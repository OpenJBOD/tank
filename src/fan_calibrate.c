/*
 * Best-effort fan characterization for the EMC2301 (see fan_calibrate.h).
 */

#include "fan_calibrate.h"
#include "fan_control.h"
#include "emc2301.h"
#include "settings.h"
#include "temperature.h"
#include "sr_latch.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(fan_cal, LOG_LEVEL_INF);

/* --- parameters --------------------------------------------------------- */
#define FAN_CAL_SETTLE_MS            3000   /* dwell after each duty change */
#define FAN_CAL_TICK_MS              250    /* abort-check granularity while dwelling */
#define FAN_CAL_READS                3      /* tach reads per step, median taken */
#define FAN_CAL_READ_GAP_MS          300
#define FAN_CAL_STOP_SETTLE_MS       4000   /* max wait for the tach to go quiet at 0 % */
#define FAN_CAL_MAX_TOTAL_MS         120000
#define FAN_CAL_BELOW_TARGET_MAX_MS  10000  /* budget for duty below the curve's target */
#define FAN_CAL_TEMP_ABORT_C         55.0f
#define FAN_CAL_TEMP_RISE_ABORT_C    8.0f
#define FAN_CAL_MONO_TOL_PERCENT     3
#define FAN_CAL_FLAT_MIN_SPAN_PCT    10
#define FAN_CAL_RPM_PLAUS_MIN        300
#define FAN_CAL_RPM_PLAUS_MAX        30000
#define FAN_CAL_FLOOR_MARGIN_PCT     150    /* choose the largest m whose floor*1.5 <= min RPM */
#define FAN_CAL_LOW_SPEED_RPM        2000   /* below this at 30 %: sample 3 edges to read slow fans */
#define FAN_CAL_MIN_DRIVE_MARGIN     5      /* persisted min drive = min spin + this */

static const uint8_t sweep_steps[FAN_CAL_SWEEP_POINTS] = { 30, 40, 50, 60, 70, 80, 90, 100 };
static const uint8_t start_steps[] = { 10, 15, 20, 25 };
static const uint8_t retry_steps[] = { 30, 60, 100 };
static const uint32_t retry_bases[] = { 19531, 4882, 2441 };

/* --- state -------------------------------------------------------------- */
static struct {
	struct k_mutex lock;              /* guards the fields below for readers */
	enum fan_cal_state state;
	enum fan_cal_phase phase;
	uint8_t progress;
	int64_t t_start;
	int64_t t_end;
	const char *abort_reason;
	atomic_t abort_flag;
	struct fan_cal_result result;

	/* run context (fan-control thread only) */
	uint8_t prev_duty;
	struct emc2301_config prev_cfg;
	struct emc2301_config run_cfg;
	bool have_start_temp;
	float start_temp;
	uint8_t curve_target;
	uint8_t cur_percent;
	uint32_t below_budget_ms;
} cal = {
	.lock = Z_MUTEX_INITIALIZER(cal.lock),
	.state = FAN_CAL_IDLE,
};

const char *fan_cal_state_str(enum fan_cal_state s)
{
	switch (s) {
	case FAN_CAL_IDLE: return "idle";
	case FAN_CAL_PENDING: return "pending";
	case FAN_CAL_RUNNING: return "running";
	case FAN_CAL_DONE: return "done";
	case FAN_CAL_ABORTED: return "aborted";
	case FAN_CAL_ERROR: return "error";
	default: return "?";
	}
}

const char *fan_cal_phase_str(enum fan_cal_phase p)
{
	switch (p) {
	case FAN_CAL_PH_NONE: return "none";
	case FAN_CAL_PH_TACH_CHECK: return "tach_check";
	case FAN_CAL_PH_SWEEP_UP: return "sweep_up";
	case FAN_CAL_PH_START_SCAN: return "start_scan";
	case FAN_CAL_PH_FREQ_RETRY: return "freq_retry";
	case FAN_CAL_PH_FINALIZE: return "finalize";
	default: return "?";
	}
}

static void set_phase(enum fan_cal_phase ph, uint8_t progress)
{
	k_mutex_lock(&cal.lock, K_FOREVER);
	cal.phase = ph;
	cal.progress = progress;
	k_mutex_unlock(&cal.lock);
}

static void set_progress(uint8_t progress)
{
	k_mutex_lock(&cal.lock, K_FOREVER);
	cal.progress = progress;
	k_mutex_unlock(&cal.lock);
}

/* --- public control ----------------------------------------------------- */

static int current_temperature(float *out)
{
	struct temperature_data td;
	const char *src;

	if (temperature_read_cached(&td) != 0) {
		return -ENODATA;
	}
	return temperature_get_active(&td, openjbod_settings_get()->environment.primary_temp_source,
				      out, &src);
}

int fan_calibrate_request(void)
{
	float t;
	int rc = 0;

	if (!emc2301_is_initialized()) {
		return -ENODEV;
	}

	k_mutex_lock(&cal.lock, K_FOREVER);
	if (cal.state == FAN_CAL_PENDING || cal.state == FAN_CAL_RUNNING) {
		rc = -EBUSY;
	} else if (current_temperature(&t) == 0 && t > FAN_CAL_TEMP_ABORT_C) {
		LOG_WRN("Fan characterization refused: temperature %.1f C above %.1f C",
			(double)t, (double)FAN_CAL_TEMP_ABORT_C);
		rc = -EHOSTDOWN;
	} else {
		cal.state = FAN_CAL_PENDING;
		cal.phase = FAN_CAL_PH_NONE;
		cal.progress = 0;
		cal.abort_reason = NULL;
		atomic_clear(&cal.abort_flag);
	}
	k_mutex_unlock(&cal.lock);

	if (rc == 0) {
		fan_control_kick();
	}
	return rc;
}

int fan_calibrate_abort(void)
{
	int rc = 0;

	k_mutex_lock(&cal.lock, K_FOREVER);
	if (cal.state == FAN_CAL_RUNNING) {
		atomic_set(&cal.abort_flag, 1);
	} else if (cal.state == FAN_CAL_PENDING) {
		cal.state = FAN_CAL_ABORTED;
		cal.abort_reason = "user";
	} else {
		rc = -EINVAL;
	}
	k_mutex_unlock(&cal.lock);
	return rc;
}

bool fan_calibrate_pending(void)
{
	return cal.state == FAN_CAL_PENDING;
}

bool fan_calibrate_running(void)
{
	return cal.state == FAN_CAL_RUNNING;
}

void fan_calibrate_get_status(struct fan_cal_status *out)
{
	if (!out) {
		return;
	}
	k_mutex_lock(&cal.lock, K_FOREVER);
	out->state = cal.state;
	out->phase = cal.phase;
	out->progress_percent = cal.progress;
	if (cal.state == FAN_CAL_RUNNING) {
		out->elapsed_ms = (uint32_t)(k_uptime_get() - cal.t_start);
	} else if (cal.t_end > cal.t_start) {
		out->elapsed_ms = (uint32_t)(cal.t_end - cal.t_start);
	} else {
		out->elapsed_ms = 0;
	}
	out->abort_reason = cal.abort_reason;
	out->result = cal.result;
	k_mutex_unlock(&cal.lock);
}

/* --- run helpers (fan-control thread) ----------------------------------- */

/* Returns an abort reason, or NULL to keep going. */
static const char *check_abort(void)
{
	float t;

	if (atomic_get(&cal.abort_flag)) {
		return "user";
	}
	if (k_uptime_get() - cal.t_start > FAN_CAL_MAX_TOTAL_MS) {
		return "timeout";
	}
	if (current_temperature(&t) == 0) {
		if (t > FAN_CAL_TEMP_ABORT_C ||
		    (cal.have_start_temp && t > cal.start_temp + FAN_CAL_TEMP_RISE_ABORT_C)) {
			return "temperature";
		}
	}
	return NULL;
}

/* Sleep for @p ms in small ticks, refreshing the temperature cache once and
 * checking the abort conditions. Accounts for time spent below the curve target. */
static const char *dwell(uint32_t ms)
{
	struct temperature_data td;
	int64_t t0 = k_uptime_get();
	bool temp_done = false;

	while (k_uptime_get() - t0 < ms) {
		const char *why = check_abort();

		if (why) {
			return why;
		}
		if (!temp_done) {
			/* One blocking probe read per dwell keeps the shared cache warm. */
			if (temperature_read(&td) == 0) {
				temperature_cache_store(&td);
			}
			temp_done = true;
		} else {
			k_msleep(FAN_CAL_TICK_MS);
		}
	}
	if (cal.cur_percent < cal.curve_target) {
		cal.below_budget_ms += (uint32_t)(k_uptime_get() - t0);
	}
	return NULL;
}

static int set_percent(uint8_t percent)
{
	int ret = emc2301_set_pwm_duty(emc2301_percent_to_duty(percent));

	if (ret == 0) {
		cal.cur_percent = percent;
	}
	return ret;
}

/* Median of FAN_CAL_READS tach counts, converted under the run configuration. */
static int measure(uint16_t *rpm, bool *valid)
{
	uint16_t counts[FAN_CAL_READS];
	bool v[FAN_CAL_READS];
	int nvalid = 0;

	for (int i = 0; i < FAN_CAL_READS; i++) {
		int ret = emc2301_read_tach(&counts[i], &v[i]);

		if (ret < 0) {
			return ret;
		}
		if (v[i]) {
			nvalid++;
		}
		if (i + 1 < FAN_CAL_READS) {
			k_msleep(FAN_CAL_READ_GAP_MS);
		}
	}

	/* Majority must be valid; median over the valid counts. */
	if (nvalid * 2 <= FAN_CAL_READS) {
		*valid = false;
		*rpm = 0;
		return 0;
	}
	uint16_t sorted[FAN_CAL_READS];
	int n = 0;

	for (int i = 0; i < FAN_CAL_READS; i++) {
		if (v[i]) {
			sorted[n++] = counts[i];
		}
	}
	for (int i = 1; i < n; i++) {
		uint16_t key = sorted[i];
		int j = i - 1;

		while (j >= 0 && sorted[j] > key) {
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = key;
	}
	*valid = true;
	*rpm = emc2301_count_to_rpm(sorted[n / 2], &cal.run_cfg);
	return 0;
}

/* Set duty, dwell, measure. Returns 0, -EIO on I2C failure, or -ECANCELED with
 * *why set on abort. */
static int step(uint8_t percent, uint16_t *rpm, bool *valid, const char **why)
{
	if (set_percent(percent) < 0) {
		return -EIO;
	}
	*why = dwell(FAN_CAL_SETTLE_MS);
	if (*why) {
		return -ECANCELED;
	}
	if (measure(rpm, valid) < 0) {
		return -EIO;
	}
	LOG_INF("fan cal: %3u%% -> %s%u RPM", percent, *valid ? "" : "no tach, ", *rpm);
	return 0;
}

static bool sweep_monotonic(const uint16_t *rpm, int n)
{
	for (int i = 1; i < n; i++) {
		uint32_t floor = (uint32_t)rpm[i - 1] * (100 - FAN_CAL_MONO_TOL_PERCENT) / 100;

		if (rpm[i] < floor) {
			return false;
		}
	}
	return true;
}

static uint8_t sweep_span_percent(const uint16_t *rpm, int n)
{
	uint16_t lo = UINT16_MAX, hi = 0;

	for (int i = 0; i < n; i++) {
		if (rpm[i] < lo) {
			lo = rpm[i];
		}
		if (rpm[i] > hi) {
			hi = rpm[i];
		}
	}
	if (hi == 0) {
		return 0;
	}
	return (uint8_t)(((uint32_t)(hi - lo) * 100) / hi);
}

/* Score a short sweep: span when monotonic, else 0. */
static uint8_t sweep_score(const uint16_t *rpm, int n)
{
	return sweep_monotonic(rpm, n) ? sweep_span_percent(rpm, n) : 0;
}

static void finish(enum fan_cal_state st, const char *reason)
{
	/* Restore the caller's configuration on abort/error; on success the
	 * persisted (possibly updated) configuration was already applied. */
	if (st != FAN_CAL_DONE) {
		(void)emc2301_apply_config(&cal.prev_cfg);
	}
	(void)emc2301_set_pwm_duty(cal.prev_duty);

	k_mutex_lock(&cal.lock, K_FOREVER);
	cal.state = st;
	cal.abort_reason = reason;
	cal.t_end = k_uptime_get();
	cal.progress = (st == FAN_CAL_DONE) ? 100 : cal.progress;
	k_mutex_unlock(&cal.lock);

	LOG_INF("fan cal: %s%s%s (%u ms)", fan_cal_state_str(st), reason ? ", " : "",
		reason ? reason : "", (unsigned)(cal.t_end - cal.t_start));
}

void fan_calibrate_run(void)
{
	struct fan_cal_result *r = &cal.result;
	const struct environment_settings *env = &openjbod_settings_get()->environment;
	const char *why = NULL;
	uint16_t rpm;
	bool valid;
	int ret;

	k_mutex_lock(&cal.lock, K_FOREVER);
	if (cal.state != FAN_CAL_PENDING) {
		k_mutex_unlock(&cal.lock);
		return;
	}
	cal.state = FAN_CAL_RUNNING;
	cal.phase = FAN_CAL_PH_NONE;
	cal.progress = 0;
	cal.t_start = k_uptime_get();
	cal.t_end = cal.t_start;
	cal.abort_reason = NULL;
	memset(r, 0, sizeof(*r));
	k_mutex_unlock(&cal.lock);

	/* --- enter: snapshot and measurement configuration ------------------- */
	(void)emc2301_get_pwm_duty(&cal.prev_duty);
	emc2301_get_config(&cal.prev_cfg);
	fan_control_hw_config_from_settings(env, &cal.run_cfg);
	cal.run_cfg.tach_range_mult = 1;  /* widest range while measuring */
	cal.run_cfg.tach_edges = 0;       /* datasheet edges for the fan's pulses/rev */
	cal.have_start_temp = (current_temperature(&cal.start_temp) == 0);
	cal.curve_target = fan_control_current_target();
	cal.cur_percent = cal.prev_duty ? emc2301_duty_to_percent(cal.prev_duty) : 0;
	cal.below_budget_ms = 0;

	r->assumed_ppr = cal.run_cfg.tach_pulses_per_rev;
	r->suggested_ppr = r->assumed_ppr;
	r->atx_power_on = sr_latch_get_state();
	r->chosen_pwm_base_hz = emc2301_snap_pwm_base_hz(cal.run_cfg.pwm_base_hz);
	r->chosen_edges = emc2301_config_edges(&cal.run_cfg);
	r->chosen_range = 1;
	memcpy(r->sweep_percent, sweep_steps, sizeof(sweep_steps));

	LOG_INF("fan cal: start (pwm %u Hz, %u ppr, ATX %s, curve target %u%%)",
		r->chosen_pwm_base_hz, r->assumed_ppr, r->atx_power_on ? "on" : "off",
		cal.curve_target);

	if (emc2301_apply_config(&cal.run_cfg) < 0) {
		finish(FAN_CAL_ERROR, "i2c");
		return;
	}

	/* --- tach check ------------------------------------------------------ */
	set_phase(FAN_CAL_PH_TACH_CHECK, 2);
	ret = step(50, &rpm, &valid, &why);
	if (ret == -ECANCELED) { finish(FAN_CAL_ABORTED, why); return; }
	if (ret < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
	if (!valid) {
		ret = step(100, &rpm, &valid, &why);
		if (ret == -ECANCELED) { finish(FAN_CAL_ABORTED, why); return; }
		if (ret < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
	}
	if (!valid) {
		r->tach_present = false;
		snprintf(r->note, sizeof(r->note), "no tach signal%s",
			 r->atx_power_on ? "" : " (ATX power is off)");
		finish(FAN_CAL_ERROR, "no_tach");
		return;
	}
	r->tach_present = true;

	/* --- sweep up -------------------------------------------------------- */
	set_phase(FAN_CAL_PH_SWEEP_UP, 5);
	for (int i = 0; i < FAN_CAL_SWEEP_POINTS; i++) {
		ret = step(sweep_steps[i], &rpm, &valid, &why);
		if (ret == -ECANCELED) { finish(FAN_CAL_ABORTED, why); return; }
		if (ret < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
		r->sweep_rpm[i] = valid ? rpm : 0;
		set_progress((uint8_t)(5 + (i + 1) * 7));
	}
	r->max_rpm = r->sweep_rpm[FAN_CAL_SWEEP_POINTS - 1];

	/* Slow fans: the datasheet edge count cannot read below ~480 RPM (m = 1).
	 * Sampling 3 edges shortens the window (floor / (ppr)), so idle speeds of
	 * quiet 120/140 mm fans stay visible. Applied now so the start scan below
	 * can see slow rotation too. */
	if (r->sweep_rpm[0] > 0 && r->sweep_rpm[0] < FAN_CAL_LOW_SPEED_RPM &&
	    emc2301_config_edges(&cal.run_cfg) > 3) {
		cal.run_cfg.tach_edges = 3;
		if (emc2301_apply_config(&cal.run_cfg) < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
		r->chosen_edges = 3;
		LOG_INF("fan cal: slow fan (%u RPM at 30%%), sampling 3 edges (floor %u RPM)",
			r->sweep_rpm[0], emc2301_config_min_rpm(&cal.run_cfg));
	}

	/* --- start scan (from standstill) ----------------------------------- */
	set_phase(FAN_CAL_PH_START_SCAN, 62);
	r->min_spin_percent = 30;  /* known to spin from the sweep */
	if (set_percent(0) < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
	{
		int64_t t0 = k_uptime_get();
		uint16_t c;
		bool v = true;

		/* Wait for the tach to go quiet; some server fans never stop at 0 %. */
		while (v && k_uptime_get() - t0 < FAN_CAL_STOP_SETTLE_MS) {
			why = dwell(FAN_CAL_TICK_MS * 2);
			if (why) { finish(FAN_CAL_ABORTED, why); return; }
			if (emc2301_read_tach(&c, &v) < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
		}
		if (v) {
			LOG_INF("fan cal: fan keeps spinning at 0%% duty");
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(start_steps); i++) {
		if (cal.below_budget_ms > FAN_CAL_BELOW_TARGET_MAX_MS) {
			r->budget_cutoff = true;
			LOG_INF("fan cal: start scan cut short by thermal budget");
			break;
		}
		ret = step(start_steps[i], &rpm, &valid, &why);
		if (ret == -ECANCELED) { finish(FAN_CAL_ABORTED, why); return; }
		if (ret < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
		if (valid && rpm > 0) {
			r->min_spin_percent = start_steps[i];
			break;
		}
		set_progress((uint8_t)(62 + (i + 1) * 5));
	}

	/* --- evaluate / frequency retry ------------------------------------- */
	r->monotonic = sweep_monotonic(r->sweep_rpm, FAN_CAL_SWEEP_POINTS);
	uint8_t span = sweep_span_percent(r->sweep_rpm, FAN_CAL_SWEEP_POINTS);
	bool degenerate = !r->monotonic || span < FAN_CAL_FLAT_MIN_SPAN_PCT;

	if (degenerate) {
		uint32_t best_base = r->chosen_pwm_base_hz;
		uint8_t best_score = sweep_score(r->sweep_rpm, FAN_CAL_SWEEP_POINTS);

		set_phase(FAN_CAL_PH_FREQ_RETRY, 85);
		r->used_retry = true;
		LOG_INF("fan cal: response at %u Hz is %s (span %u%%), trying other bases",
			r->chosen_pwm_base_hz, r->monotonic ? "flat" : "non-monotonic", span);

		for (size_t b = 0; b < ARRAY_SIZE(retry_bases); b++) {
			uint16_t rr[ARRAY_SIZE(retry_steps)];

			if (retry_bases[b] == r->chosen_pwm_base_hz) {
				continue;
			}
			cal.run_cfg.pwm_base_hz = retry_bases[b];
			if (emc2301_apply_config(&cal.run_cfg) < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
			for (size_t i = 0; i < ARRAY_SIZE(retry_steps); i++) {
				ret = step(retry_steps[i], &rpm, &valid, &why);
				if (ret == -ECANCELED) { finish(FAN_CAL_ABORTED, why); return; }
				if (ret < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
				rr[i] = valid ? rpm : 0;
			}
			uint8_t score = sweep_score(rr, ARRAY_SIZE(retry_steps));

			LOG_INF("fan cal: %u Hz score %u", retry_bases[b], score);
			if (score > best_score) {
				best_score = score;
				best_base = retry_bases[b];
			}
			set_progress((uint8_t)(85 + (b + 1) * 3));
		}
		r->chosen_pwm_base_hz = best_base;
		cal.run_cfg.pwm_base_hz = best_base;
		if (emc2301_apply_config(&cal.run_cfg) < 0) { finish(FAN_CAL_ERROR, "i2c"); return; }
		if (best_base != emc2301_snap_pwm_base_hz(env->fan_pwm_base_hz)) {
			snprintf(r->note, sizeof(r->note), "%s at %u Hz, using %u Hz",
				 r->monotonic ? "flat response" : "non-monotonic",
				 emc2301_snap_pwm_base_hz(env->fan_pwm_base_hz), best_base);
		} else {
			snprintf(r->note, sizeof(r->note), "%s response at every PWM frequency",
				 r->monotonic ? "flat" : "non-monotonic");
		}
	}

	/* --- finalize -------------------------------------------------------- */
	set_phase(FAN_CAL_PH_FINALIZE, 96);
	{
		/* Lowest speed the fan was seen running at, for the RANGE choice. */
		uint16_t min_rpm = UINT16_MAX;

		for (int i = 0; i < FAN_CAL_SWEEP_POINTS; i++) {
			if (r->sweep_rpm[i] > 0 && r->sweep_rpm[i] < min_rpm) {
				min_rpm = r->sweep_rpm[i];
			}
		}
		if (min_rpm == UINT16_MAX) {
			min_rpm = 0;
		}
		r->chosen_range = 1;
		{
			static const uint8_t mults[] = { 8, 4, 2, 1 };
			struct emc2301_config probe = cal.run_cfg;

			for (size_t i = 0; i < ARRAY_SIZE(mults); i++) {
				probe.tach_range_mult = mults[i];
				if ((uint32_t)emc2301_config_min_rpm(&probe) * FAN_CAL_FLOOR_MARGIN_PCT / 100
				    <= min_rpm) {
					r->chosen_range = mults[i];
					break;
				}
			}
		}

		/* Pulses/rev cannot be measured; only flag an implausible result. */
		if (r->max_rpm > FAN_CAL_RPM_PLAUS_MAX) {
			r->suggested_ppr = (uint8_t)MIN(r->assumed_ppr * 2, 4);
		} else if (r->max_rpm > 0 && r->max_rpm < FAN_CAL_RPM_PLAUS_MIN) {
			r->suggested_ppr = (uint8_t)MAX(r->assumed_ppr / 2, 1);
		}

		if (r->suggested_ppr != r->assumed_ppr) {
			r->confidence = 0;
			snprintf(r->note, sizeof(r->note), "%u RPM implausible for %u pulses/rev; try %u",
				 r->max_rpm, r->assumed_ppr, r->suggested_ppr);
		} else if (!r->monotonic && r->used_retry) {
			r->confidence = 0;
		} else if (r->used_retry || r->budget_cutoff ||
			   r->max_rpm < 1000 || r->max_rpm > 20000) {
			r->confidence = 1;
		} else {
			r->confidence = 2;
		}
		if (r->note[0] == '\0') {
			snprintf(r->note, sizeof(r->note), "ok: %u..%u RPM, starts at %u%%",
				 min_rpm, r->max_rpm, r->min_spin_percent);
		}
	}

	/* --- persist + apply -------------------------------------------------- */
	{
		struct environment_settings new_env = *env;

		new_env.fan_pwm_base_hz = r->chosen_pwm_base_hz;
		new_env.fan_tach_edges = (r->chosen_edges == emc2301_edges_for_ppr(r->assumed_ppr))
					 ? 0 : r->chosen_edges;
		new_env.fan_tach_range = r->chosen_range;
		new_env.fan_min_drive_percent =
			(uint8_t)MIN(r->min_spin_percent + FAN_CAL_MIN_DRIVE_MARGIN, 100);
		new_env.fan_cal_min_spin_percent = r->min_spin_percent;
		new_env.fan_cal_max_rpm = r->max_rpm;

		ret = openjbod_settings_set_environment(&new_env);
		if (ret) {
			LOG_ERR("fan cal: failed to save results: %d", ret);
			finish(FAN_CAL_ERROR, "settings");
			return;
		}
		fan_control_hw_config_from_settings(&openjbod_settings_get()->environment,
						    &cal.run_cfg);
		if (emc2301_apply_config(&cal.run_cfg) < 0) {
			finish(FAN_CAL_ERROR, "i2c");
			return;
		}
	}

	LOG_INF("fan cal: result pwm %u Hz, %u edges, range x%u, min spin %u%%, max %u RPM, confidence %u: %s",
		r->chosen_pwm_base_hz, r->chosen_edges, r->chosen_range, r->min_spin_percent,
		r->max_rpm, r->confidence, r->note);
	finish(FAN_CAL_DONE, NULL);
}
