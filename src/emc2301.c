/*
 * EMC2301 Fan Controller Driver for OpenJBOD
 *
 * Direct Setting mode only: the firmware owns the PWM duty; the chip's RPM
 * control algorithm (EN_ALGO) stays disabled. Register layout and the tach
 * conversion follow the EMC2301 datasheet rev 1.3 / AN17.4:
 *
 *     RPM = 3932160 * m / COUNT     (valid when edges = 2 * pulses_per_rev + 1)
 *
 * where COUNT is the 13-bit tach reading and m the RANGE multiplier (1/2/4/8).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include "emc2301.h"

LOG_MODULE_REGISTER(emc2301, LOG_LEVEL_INF);

/* Device tree reference for I2C bus */
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* Driver state. The mutex serialises chip access from the fan-control thread,
 * the HTTP server, the shell and the calibration routine. */
static K_MUTEX_DEFINE(emc_lock);
static struct emc2301_data emc_data;
static struct emc2301_config applied_cfg = EMC2301_CONFIG_DEFAULTS;
static bool initialized;

#define EMC2301_I2C_RETRIES     3
#define EMC2301_I2C_RETRY_MS    10

static const uint32_t pwm_base_table[4] = { 26000, 19531, 4882, 2441 };

/**
 * Read or write a single EMC2301 register, with retries. For a write, *value is
 * the byte to send; for a read, *value receives the byte.
 */
static int emc2301_reg_access(uint8_t reg, uint8_t *value, bool write)
{
	const char *op = write ? "write" : "read";
	int ret = -EIO;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	for (int retries = EMC2301_I2C_RETRIES; retries > 0; retries--) {
		ret = write ? i2c_reg_write_byte(i2c_dev, EMC2301_I2C_ADDR, reg, *value)
			    : i2c_reg_read_byte(i2c_dev, EMC2301_I2C_ADDR, reg, value);
		if (ret >= 0) {
			LOG_DBG("I2C %s: reg=0x%02x, value=0x%02x", op, reg, *value);
			return ret;
		}

		if (retries > 1) {
			LOG_WRN("I2C %s retry: reg=0x%02x, ret=%d, retries left=%d",
				op, reg, ret, retries - 1);
			k_msleep(EMC2301_I2C_RETRY_MS);
		}
	}

	LOG_ERR("I2C %s failed: reg=0x%02x, ret=%d", op, reg, ret);
	return ret;
}

static int emc2301_reg_read(uint8_t reg, uint8_t *value)
{
	return emc2301_reg_access(reg, value, false);
}

static int emc2301_reg_write(uint8_t reg, uint8_t value)
{
	return emc2301_reg_access(reg, &value, true);
}

/* Write a register and confirm it read back as expected (masked). */
static int emc2301_reg_write_verify(uint8_t reg, uint8_t value, uint8_t mask)
{
	uint8_t rb;
	int ret = emc2301_reg_write(reg, value);

	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_read(reg, &rb);
	if (ret < 0) {
		return ret;
	}
	if ((rb & mask) != (value & mask)) {
		LOG_ERR("Register 0x%02x read back 0x%02x, expected 0x%02x", reg, rb, value);
		return -EIO;
	}
	return 0;
}

/**
 * Read the tach reading pair. The high byte must be read first: the chip
 * latches the low byte into a shadow register on that access.
 */
static int emc2301_read_tach_raw(uint16_t *raw)
{
	uint8_t msb, lsb;
	int ret;

	ret = emc2301_reg_read(EMC2301_REG_TACH_READING_MSB, &msb);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_read(EMC2301_REG_TACH_READING_LSB, &lsb);
	if (ret < 0) {
		return ret;
	}
	*raw = (uint16_t)((msb << 8) | lsb);
	return 0;
}

/**
 * Verify EMC2301 device identification
 */
static int emc2301_verify_device(void)
{
	uint8_t mfg_id, product_id;
	int ret;

	ret = emc2301_reg_read(EMC2301_REG_MANUFACTURER, &mfg_id);
	if (ret < 0) {
		LOG_ERR("Failed to read manufacturer ID: %d", ret);
		return ret;
	}

	ret = emc2301_reg_read(EMC2301_REG_PRODUCT_ID, &product_id);
	if (ret < 0) {
		LOG_ERR("Failed to read product ID: %d", ret);
		return ret;
	}

	if (mfg_id != EMC2301_MFG_ID) {
		LOG_ERR("Invalid manufacturer ID: 0x%02x (expected 0x%02x)",
			mfg_id, EMC2301_MFG_ID);
		return -ENODEV;
	}

	if (product_id != EMC2301_PRODUCT_ID) {
		LOG_ERR("Invalid product ID: 0x%02x (expected 0x%02x)",
			product_id, EMC2301_PRODUCT_ID);
		return -ENODEV;
	}

	LOG_INF("EMC2301 device verified (MFG: 0x%02x, PID: 0x%02x)",
		mfg_id, product_id);
	return 0;
}

/* --- configuration helpers ---------------------------------------------- */

uint32_t emc2301_snap_pwm_base_hz(uint32_t hz)
{
	uint32_t best = pwm_base_table[0];
	uint32_t best_diff = UINT32_MAX;

	for (size_t i = 0; i < ARRAY_SIZE(pwm_base_table); i++) {
		uint32_t d = hz > pwm_base_table[i] ? hz - pwm_base_table[i]
						    : pwm_base_table[i] - hz;
		if (d < best_diff) {
			best_diff = d;
			best = pwm_base_table[i];
		}
	}
	return best;
}

static enum emc2301_pwm_base pwm_base_enum(uint32_t snapped_hz)
{
	for (size_t i = 0; i < ARRAY_SIZE(pwm_base_table); i++) {
		if (pwm_base_table[i] == snapped_hz) {
			return (enum emc2301_pwm_base)i;
		}
	}
	return EMC2301_PWM_BASE_26000;
}

static int range_enum(uint8_t mult, enum emc2301_tach_range *out)
{
	switch (mult) {
	case 1: *out = EMC2301_RANGE_500; return 0;
	case 2: *out = EMC2301_RANGE_1000; return 0;
	case 4: *out = EMC2301_RANGE_2000; return 0;
	case 8: *out = EMC2301_RANGE_4000; return 0;
	default: return -EINVAL;
	}
}

static int edges_enum(uint8_t edges, enum emc2301_tach_edges *out)
{
	if (edges != 3 && edges != 5 && edges != 7 && edges != 9) {
		return -EINVAL;
	}
	*out = (enum emc2301_tach_edges)((edges - 3) / 2);
	return 0;
}

uint8_t emc2301_config_edges(const struct emc2301_config *cfg)
{
	return cfg->tach_edges ? cfg->tach_edges : emc2301_edges_for_ppr(cfg->tach_pulses_per_rev);
}

/* Validate and normalise a configuration into *out. */
static int normalise_config(const struct emc2301_config *in, struct emc2301_config *out)
{
	enum emc2301_tach_range range;
	enum emc2301_tach_edges edges;

	if (in == NULL) {
		return -EINVAL;
	}
	if (in->tach_pulses_per_rev < 1 || in->tach_pulses_per_rev > 4 ||
	    range_enum(in->tach_range_mult, &range) != 0 ||
	    edges_enum(emc2301_config_edges(in), &edges) != 0 ||
	    in->min_drive_percent > 100) {
		return -EINVAL;
	}

	*out = *in;
	out->pwm_base_hz = emc2301_snap_pwm_base_hz(in->pwm_base_hz);
	if (out->pwm_divide == 0) {
		out->pwm_divide = 1;  /* the chip decodes 0 as 1 anyway */
	}
	return 0;
}

static int apply_config_locked(const struct emc2301_config *cfg)
{
	struct emc2301_config c;
	enum emc2301_tach_range range = EMC2301_RANGE_500;
	enum emc2301_tach_edges edges = EMC2301_EDGES_5;
	uint8_t cfg1;
	int ret;

	ret = normalise_config(cfg, &c);
	if (ret < 0) {
		return ret;
	}
	(void)range_enum(c.tach_range_mult, &range);
	(void)edges_enum(emc2301_config_edges(&c), &edges);

	cfg1 = (uint8_t)(((uint8_t)range << EMC2301_FAN_CFG1_RANGE_SHIFT) |
			 ((uint8_t)edges << EMC2301_FAN_CFG1_EDGES_SHIFT) |
			 EMC2301_FAN_CFG1_UPDATE_400MS);  /* EN_ALGO = 0: direct PWM mode */

	/* Normal polarity, push-pull output (the board drives the fan's PWM pin directly). */
	ret = emc2301_reg_write_verify(EMC2301_REG_PWM_POLARITY, 0x00, 0x01);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_write_verify(EMC2301_REG_PWM_OUT_CONFIG, 0x01, 0x01);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_write_verify(EMC2301_REG_PWM_BASE_FREQ,
				       (uint8_t)pwm_base_enum(c.pwm_base_hz),
				       EMC2301_PWM_BASE_MASK);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_write_verify(EMC2301_REG_PWM_DIVIDE, c.pwm_divide, 0xFF);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_reg_write_verify(EMC2301_REG_FAN_CONFIG1, cfg1, 0xFF);
	if (ret < 0) {
		return ret;
	}
	/* Only consulted by the FSC algorithm, but keep it consistent with settings. */
	ret = emc2301_reg_write(EMC2301_REG_MIN_DRIVE,
				emc2301_percent_to_duty(c.min_drive_percent));
	if (ret < 0) {
		return ret;
	}

	applied_cfg = c;
	LOG_INF("fan hw: pwm %u Hz (/%u), %u ppr, %u edges, range x%u (min %u RPM), CFG1=0x%02x",
		c.pwm_base_hz, c.pwm_divide, c.tach_pulses_per_rev, emc2301_config_edges(&c),
		c.tach_range_mult, emc2301_config_min_rpm(&c), cfg1);
	return 0;
}

int emc2301_apply_config(const struct emc2301_config *cfg)
{
	int ret;

	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = apply_config_locked(cfg);
	k_mutex_unlock(&emc_lock);
	return ret;
}

void emc2301_get_config(struct emc2301_config *out)
{
	if (out) {
		k_mutex_lock(&emc_lock, K_FOREVER);
		*out = applied_cfg;
		k_mutex_unlock(&emc_lock);
	}
}

uint32_t emc2301_effective_pwm_hz(void)
{
	uint8_t div = applied_cfg.pwm_divide ? applied_cfg.pwm_divide : 1;

	return applied_cfg.pwm_base_hz / div;
}

uint16_t emc2301_count_to_rpm(uint16_t count13, const struct emc2301_config *cfg)
{
	uint32_t num, den, rpm;

	if (count13 == 0 || count13 >= EMC2301_TACH_VALID_MAX || cfg == NULL ||
	    cfg->tach_pulses_per_rev == 0) {
		return 0;
	}
	/* (edges-1) * m <= 64, so the numerator stays below 2^27. */
	num = (uint32_t)(emc2301_config_edges(cfg) - 1) * cfg->tach_range_mult *
	      (EMC2301_TACH_RPM_CONST / 2);
	den = (uint32_t)cfg->tach_pulses_per_rev * count13;
	rpm = num / den;
	if (rpm > UINT16_MAX) {
		rpm = UINT16_MAX;
	}
	return (uint16_t)rpm;
}

uint16_t emc2301_config_min_rpm(const struct emc2301_config *cfg)
{
	return emc2301_count_to_rpm(EMC2301_TACH_VALID_MAX - 1, cfg);
}

bool emc2301_is_initialized(void)
{
	return initialized;
}

/* --- init --------------------------------------------------------------- */

int emc2301_init(const struct emc2301_config *cfg)
{
	struct emc2301_config defaults = EMC2301_CONFIG_DEFAULTS;
	uint8_t reg;
	int ret;

	LOG_INF("Initializing EMC2301 fan controller");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	ret = emc2301_verify_device();
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&emc_lock, K_FOREVER);

	/* Disable the SMBus timeout only. The CLK pin is unconnected on OpenJBOD,
	 * so neither clock-output nor external-clock bits are set. */
	ret = emc2301_reg_write_verify(EMC2301_REG_CONFIG, EMC2301_CONFIG_DIS_TO, 0xFF);
	if (ret < 0) {
		LOG_ERR("Failed to write configuration register: %d", ret);
		goto out;
	}

	ret = apply_config_locked(cfg ? cfg : &defaults);
	if (ret == -EINVAL) {
		LOG_WRN("Invalid fan configuration, falling back to defaults");
		ret = apply_config_locked(&defaults);
	}
	if (ret < 0) {
		LOG_ERR("Failed to apply fan configuration: %d", ret);
		goto out;
	}

	/* Status registers are read-to-clear; drop anything latched since power-up. */
	(void)emc2301_reg_read(EMC2301_REG_FAN_STATUS, &reg);
	(void)emc2301_reg_read(EMC2301_REG_FAN_STALL, &reg);
	(void)emc2301_reg_read(EMC2301_REG_FAN_SPIN, &reg);
	(void)emc2301_reg_read(EMC2301_REG_DRIVE_FAIL, &reg);

	initialized = true;
	emc_data.initialized = true;
	ret = 0;

out:
	k_mutex_unlock(&emc_lock);
	if (ret < 0) {
		return ret;
	}

	/* Initial fan speed: 25 % until the fan-control loop takes over. */
	ret = emc2301_set_pwm_duty(emc2301_percent_to_duty(25));
	if (ret < 0) {
		LOG_WRN("Failed to set initial fan speed: %d", ret);
	}

	LOG_INF("EMC2301 initialized successfully");
	LOG_INF("Note: a stall flag is normal if no fan is physically connected");
	return 0;
}

/* --- duty --------------------------------------------------------------- */

static int set_pwm_duty_locked(uint8_t duty)
{
	uint8_t readback;
	bool from_stopped = (emc_data.pwm_duty == 0 && duty != 0);
	int ret;

	ret = emc2301_reg_write(EMC2301_REG_FAN_DRIVE, duty);
	if (ret < 0) {
		LOG_ERR("Failed to set PWM duty cycle: %d", ret);
		return ret;
	}

	/* Leaving 0 % starts the chip's Spin Up Routine: for the spin-up time
	 * (default 500 ms) the drive register reports the spin-up level, not the
	 * value just written, so a read-back cannot be used to verify it. */
	if (from_stopped) {
		emc_data.pwm_duty = duty;
		LOG_DBG("PWM duty set to %d from stopped; spin-up routine running", duty);
		return 0;
	}

	k_msleep(1);
	ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, &readback);
	if (ret >= 0 && readback != duty) {
		LOG_WRN("PWM duty mismatch: wrote %d, read %d; retrying", duty, readback);
		ret = emc2301_reg_write(EMC2301_REG_FAN_DRIVE, duty);
		if (ret < 0) {
			LOG_ERR("Retry failed to set PWM duty cycle: %d", ret);
			return ret;
		}
		k_msleep(1);
		ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, &readback);
		if (ret >= 0 && readback != duty) {
			LOG_ERR("PWM duty still mismatched after retry: %d", readback);
			return -EIO;
		}
	}

	emc_data.pwm_duty = duty;
	LOG_DBG("PWM duty set to %d (%d%%)", duty, emc2301_duty_to_percent(duty));
	return 0;
}

int emc2301_set_pwm_duty(uint8_t duty)
{
	int ret;

	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = set_pwm_duty_locked(duty);
	k_mutex_unlock(&emc_lock);
	return ret;
}

int emc2301_get_pwm_duty(uint8_t *duty)
{
	int ret;

	if (duty == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, duty);
	if (ret < 0) {
		LOG_ERR("Failed to read PWM duty cycle: %d", ret);
		*duty = emc_data.pwm_duty;  /* fall back to the cached value */
	} else {
		emc_data.pwm_duty = *duty;
	}
	k_mutex_unlock(&emc_lock);
	return 0;
}

/* --- tach --------------------------------------------------------------- */

static int read_tach_locked(uint16_t *count13, bool *valid)
{
	uint16_t raw;
	int ret;

	ret = emc2301_read_tach_raw(&raw);
	if (ret < 0) {
		return ret;
	}
	*count13 = (uint16_t)(raw >> (16 - EMC2301_TACH_COUNT_BITS));
	*valid = (*count13 != 0) && (*count13 < EMC2301_TACH_VALID_MAX);
	emc_data.tach_count = *count13;
	emc_data.tach_valid = *valid;
	LOG_DBG("Tach raw 0x%04x -> count %u (%s)", raw, *count13, *valid ? "valid" : "no signal");
	return 0;
}

int emc2301_read_tach(uint16_t *count13, bool *valid)
{
	int ret;

	if (count13 == NULL || valid == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = read_tach_locked(count13, valid);
	k_mutex_unlock(&emc_lock);
	return ret;
}

int emc2301_get_fan_speed(uint16_t *rpm)
{
	uint16_t count;
	bool valid;
	int ret;

	if (rpm == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = read_tach_locked(&count, &valid);
	if (ret < 0) {
		LOG_ERR("Failed to read tachometer: %d", ret);
		*rpm = emc_data.fan_rpm;  /* fall back to the cached value */
	} else {
		*rpm = valid ? emc2301_count_to_rpm(count, &applied_cfg) : 0;
		emc_data.fan_rpm = *rpm;
	}
	k_mutex_unlock(&emc_lock);
	return 0;
}

int emc2301_get_status(struct emc2301_data *data)
{
	uint8_t status_reg;
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}

	ret = emc2301_get_pwm_duty(&data->pwm_duty);
	if (ret < 0) {
		return ret;
	}
	ret = emc2301_get_fan_speed(&data->fan_rpm);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&emc_lock, K_FOREVER);
	data->tach_count = emc_data.tach_count;
	data->tach_valid = emc_data.tach_valid;
	ret = emc2301_reg_read(EMC2301_REG_FAN_STATUS, &status_reg);
	k_mutex_unlock(&emc_lock);
	if (ret < 0) {
		LOG_ERR("Failed to read fan status: %d", ret);
		return ret;
	}

	data->status_reg = status_reg;
	/* The chip's FNSTL bit is latched (read-to-clear) and also fires whenever the
	 * fan is deliberately stopped, so report the *current* condition instead:
	 * the fan is being driven but produces no tach signal. */
	data->stall = (data->pwm_duty > 0) && !data->tach_valid;
	data->spin_fail = (status_reg & EMC2301_FAN_STATUS_FNSPIN) != 0;
	data->drive_fail = (status_reg & EMC2301_FAN_STATUS_DVFAIL) != 0;
	data->watchdog = (status_reg & EMC2301_FAN_STATUS_WATCH) != 0;
	data->fan_fault = data->stall || data->drive_fail;
	data->initialized = initialized;

	if (data->stall) {
		LOG_DBG("Fan status: stall flag set");
	}
	if (data->watchdog) {
		LOG_DBG("Fan status: watchdog timeout");
	}
	return 0;
}

int emc2301_read_reg(uint8_t reg, uint8_t *val)
{
	int ret;

	if (val == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&emc_lock, K_FOREVER);
	ret = emc2301_reg_read(reg, val);
	k_mutex_unlock(&emc_lock);
	return ret;
}
