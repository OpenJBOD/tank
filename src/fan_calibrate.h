/*
 * Best-effort fan characterization for the EMC2301.
 *
 * Measures what can be measured safely (tach presence, RPM-vs-duty response,
 * lowest duty that starts the fan, RPM at full drive), derives the tach RANGE
 * from the lowest observed speed (AN17.4) and, only when the response at the
 * configured PWM frequency is degenerate, tries the other base frequencies.
 * Pulses per revolution cannot be measured (it only scales RPM), so it is never
 * changed automatically; an implausible result yields a suggestion instead.
 *
 * The run executes inside the fan-control thread (fan_calibrate_run()) so it
 * owns the PWM output; everything else only requests/aborts/observes.
 */

#ifndef FAN_CALIBRATE_H
#define FAN_CALIBRATE_H

#include <stdint.h>
#include <stdbool.h>

enum fan_cal_state {
	FAN_CAL_IDLE = 0,
	FAN_CAL_PENDING,
	FAN_CAL_RUNNING,
	FAN_CAL_DONE,
	FAN_CAL_ABORTED,
	FAN_CAL_ERROR,
};

enum fan_cal_phase {
	FAN_CAL_PH_NONE = 0,
	FAN_CAL_PH_TACH_CHECK,
	FAN_CAL_PH_SWEEP_UP,
	FAN_CAL_PH_START_SCAN,
	FAN_CAL_PH_FREQ_RETRY,
	FAN_CAL_PH_FINALIZE,
};

#define FAN_CAL_SWEEP_POINTS 8   /* 30..100 % in 10 % steps */
#define FAN_CAL_NOTE_LEN     64

struct fan_cal_result {
	bool tach_present;          /* a tach signal was seen at 50 or 100 % */
	bool monotonic;             /* RPM rose with duty at the chosen frequency */
	bool atx_power_on;          /* ATX (fan) power state when the run started */
	bool budget_cutoff;         /* start scan shortened by the thermal budget */
	bool used_retry;            /* other PWM base frequencies were tried */
	uint8_t min_spin_percent;   /* lowest step that produced rotation (10..30) */
	uint16_t max_rpm;           /* RPM at 100 % duty */
	uint8_t sweep_percent[FAN_CAL_SWEEP_POINTS];
	uint16_t sweep_rpm[FAN_CAL_SWEEP_POINTS];
	uint32_t chosen_pwm_base_hz;
	uint8_t chosen_edges;       /* tach edges sampled: 3|5|7|9 */
	uint8_t chosen_range;       /* tach range multiplier 1|2|4|8 */
	uint8_t assumed_ppr;        /* pulses/rev the RPM figures assume */
	uint8_t suggested_ppr;      /* == assumed unless max_rpm is implausible */
	uint8_t confidence;         /* 0 low, 1 medium, 2 high */
	char note[FAN_CAL_NOTE_LEN];
};

struct fan_cal_status {
	enum fan_cal_state state;
	enum fan_cal_phase phase;
	uint8_t progress_percent;
	uint32_t elapsed_ms;
	const char *abort_reason;   /* NULL, "user", "temperature", "timeout", "i2c", "no_tach" */
	struct fan_cal_result result;
};

/**
 * Ask the fan-control thread to run a characterization.
 * @return 0, -EBUSY (already pending/running), -ENODEV (controller not
 *         initialised), -EHOSTDOWN (temperature too high to run safely)
 */
int fan_calibrate_request(void);

/** Abort a pending or running characterization. -EINVAL if none. */
int fan_calibrate_abort(void);

/** True when a run has been requested and not yet started. */
bool fan_calibrate_pending(void);

/** True while a run is executing. */
bool fan_calibrate_running(void);

/** Execute a pending run. Fan-control thread only. */
void fan_calibrate_run(void);

/** Snapshot of state, progress and the last result. */
void fan_calibrate_get_status(struct fan_cal_status *out);

const char *fan_cal_state_str(enum fan_cal_state s);
const char *fan_cal_phase_str(enum fan_cal_phase p);

#endif /* FAN_CALIBRATE_H */
