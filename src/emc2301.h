/*
 * EMC2301 Fan Controller Driver for OpenJBOD
 *
 * Register map and formulas follow the SMSC/Microchip EMC2301 datasheet
 * (rev 1.3) and AN17.4 "RPM to TACH Counts Conversion". The part is used in
 * Direct Setting mode (EN_ALGO = 0): the firmware writes the PWM duty and reads
 * the tachometer; the on-chip RPM control loop is not used.
 */

#ifndef EMC2301_H
#define EMC2301_H

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <stdbool.h>

/* EMC2301 I2C Address */
#define EMC2301_I2C_ADDR           0x2F

/* EMC2301 Register Definitions */
#define EMC2301_REG_CONFIG         0x20  /* Configuration register */
#define EMC2301_REG_FAN_STATUS     0x24  /* Fan status register (read clears) */
#define EMC2301_REG_FAN_STALL      0x25  /* Fan stall status (read clears) */
#define EMC2301_REG_FAN_SPIN       0x26  /* Fan spin status (read clears) */
#define EMC2301_REG_DRIVE_FAIL     0x27  /* Drive fail status (read clears) */
#define EMC2301_REG_FAN_INT_EN     0x29  /* Fan interrupt enable */
#define EMC2301_REG_PWM_POLARITY   0x2A  /* PWM polarity (bit0: 1 = inverted) */
#define EMC2301_REG_PWM_OUT_CONFIG 0x2B  /* PWM output type (bit0: 1 = push-pull) */
#define EMC2301_REG_PWM_BASE_FREQ  0x2D  /* PWM base frequency (bits 1:0) */
#define EMC2301_REG_FAN_DRIVE      0x30  /* Fan drive (PWM duty cycle) */
#define EMC2301_REG_PWM_DIVIDE     0x31  /* PWM frequency divide (0 decodes as 1) */
#define EMC2301_REG_FAN_CONFIG1    0x32  /* Fan configuration 1 */
#define EMC2301_REG_FAN_CONFIG2    0x33  /* Fan configuration 2 */
#define EMC2301_REG_PID_GAIN       0x35  /* PID gain settings */
#define EMC2301_REG_FAN_SPIN_UP    0x36  /* Fan spin up configuration */
#define EMC2301_REG_MAX_STEP       0x37  /* Maximum step register */
#define EMC2301_REG_MIN_DRIVE      0x38  /* Minimum drive setting (FSC mode only) */
#define EMC2301_REG_VALID_TACH     0x39  /* Valid tach count (high byte) */
#define EMC2301_REG_DRIVE_FAIL_LSB 0x3A  /* Drive fail band low byte */
#define EMC2301_REG_DRIVE_FAIL_MSB 0x3B  /* Drive fail band high byte */
#define EMC2301_REG_TACH_TARGET_LSB 0x3C /* Tach target low byte */
#define EMC2301_REG_TACH_TARGET_MSB 0x3D /* Tach target high byte */
#define EMC2301_REG_TACH_READING_MSB 0x3E /* Tach reading high byte (read first) */
#define EMC2301_REG_TACH_READING_LSB 0x3F /* Tach reading low byte */
#define EMC2301_REG_SOFTWARE_LOCK  0xEF  /* Software lock register */
#define EMC2301_REG_PRODUCT_ID     0xFD  /* Product ID register */
#define EMC2301_REG_MANUFACTURER   0xFE  /* Manufacturer ID register */
#define EMC2301_REG_REVISION       0xFF  /* Revision register */

/* Identification */
#define EMC2301_MFG_ID             0x5D  /* Expected manufacturer ID */
#define EMC2301_PRODUCT_ID         0x37  /* Expected product ID for EMC2301 */

/* Duty range of the Fan Drive register */
#define EMC2301_FAN_MAX            255
#define EMC2301_FAN_MIN            0

/* 0x20 Configuration register bits (POR 0x40) */
#define EMC2301_CONFIG_MASK        BIT(7)  /* Mask ALERT# */
#define EMC2301_CONFIG_DIS_TO      BIT(6)  /* Disable SMBus timeout */
#define EMC2301_CONFIG_WD_EN       BIT(5)  /* Watchdog continuous mode */
#define EMC2301_CONFIG_DR_EXT_CLK  BIT(1)  /* Drive internal 32 kHz clock out on CLK */
#define EMC2301_CONFIG_USE_EXT_CLK BIT(0)  /* Use clock supplied on CLK pin */

/* 0x24 Fan status register bits (read-to-clear) */
#define EMC2301_FAN_STATUS_WATCH   BIT(7)  /* Watchdog timeout occurred */
#define EMC2301_FAN_STATUS_DVFAIL  BIT(2)  /* Drive fail (FSC mode only) */
#define EMC2301_FAN_STATUS_FNSPIN  BIT(1)  /* Spin-up failed (FSC mode only) */
#define EMC2301_FAN_STATUS_FNSTL   BIT(0)  /* Tach reading exceeded Valid TACH Count */

/* 0x2D PWM base frequency, bits 1:0 */
enum emc2301_pwm_base {
	EMC2301_PWM_BASE_26000 = 0,  /* 26.00 kHz (POR default) */
	EMC2301_PWM_BASE_19531 = 1,  /* 19.531 kHz */
	EMC2301_PWM_BASE_4882  = 2,  /* 4.882 kHz */
	EMC2301_PWM_BASE_2441  = 3,  /* 2.441 kHz */
};
#define EMC2301_PWM_BASE_MASK      0x03

/* 0x32 Fan Configuration 1: EN_ALGO | RANGE[6:5] | EDGES[4:3] | UPDATE[2:0], POR 0x2B */
#define EMC2301_FAN_CFG1_EN_ALGO      BIT(7)
#define EMC2301_FAN_CFG1_RANGE_SHIFT  5
#define EMC2301_FAN_CFG1_RANGE_MASK   (0x3 << EMC2301_FAN_CFG1_RANGE_SHIFT)
#define EMC2301_FAN_CFG1_EDGES_SHIFT  3
#define EMC2301_FAN_CFG1_EDGES_MASK   (0x3 << EMC2301_FAN_CFG1_EDGES_SHIFT)
#define EMC2301_FAN_CFG1_UPDATE_MASK  0x07
#define EMC2301_FAN_CFG1_UPDATE_400MS 0x03  /* POR value */

/* RANGE[1:0]: tach count multiplier m and reported minimum RPM (500 * m) */
enum emc2301_tach_range {
	EMC2301_RANGE_500  = 0,  /* m = 1 */
	EMC2301_RANGE_1000 = 1,  /* m = 2 (POR default) */
	EMC2301_RANGE_2000 = 2,  /* m = 4 */
	EMC2301_RANGE_4000 = 3,  /* m = 8 */
};

/* EDGES[1:0]: edges sampled per measurement; correct value is 2 * pulses_per_rev + 1 */
enum emc2301_tach_edges {
	EMC2301_EDGES_3 = 0,  /* 1 pulse/rev */
	EMC2301_EDGES_5 = 1,  /* 2 pulses/rev (POR default, standard 4-wire fans) */
	EMC2301_EDGES_7 = 2,  /* 3 pulses/rev */
	EMC2301_EDGES_9 = 3,  /* 4 pulses/rev */
};

/* Tachometer reading: 13-bit COUNT in bits 15:3 of the 0x3E/0x3F pair */
#define EMC2301_TACH_COUNT_BITS    13
#define EMC2301_TACH_INVALID       0x1FFF     /* raw 0xFFF8: fan stopped or absent */
/* The saturated reading is not always exactly 0x1FFF (0x1FF8 was observed with no
 * fan and RANGE x8), so anything within this margin of the top is "no signal". */
#define EMC2301_TACH_VALID_MAX     0x1FF0
#define EMC2301_TACH_RPM_CONST     3932160UL  /* 60 * 32768 * (5 - 1) / 2 poles */
#define EMC2301_TACH_RANGE_BASE_RPM 500       /* minimum reportable RPM at m = 1 */

/* Hardware-facing fan configuration (persisted in environment settings) */
struct emc2301_config {
	uint32_t pwm_base_hz;         /* 26000 | 19531 | 4882 | 2441 (snapped by the driver) */
	uint8_t pwm_divide;           /* 1..255; effective frequency = base / divide */
	uint8_t tach_pulses_per_rev;  /* 1..4: fan property, scales the RPM figure */
	uint8_t tach_edges;           /* 0 = auto (2*ppr+1), else 3|5|7|9 edges sampled */
	uint8_t tach_range_mult;      /* 1 | 2 | 4 | 8 -> RANGE (count multiplier m) */
	uint8_t min_drive_percent;    /* 0..100, written to 0x38 and enforced by fan_control */
};

#define EMC2301_CONFIG_DEFAULTS { \
	.pwm_base_hz = 26000, .pwm_divide = 1, .tach_pulses_per_rev = 2, \
	.tach_edges = 0, .tach_range_mult = 1, .min_drive_percent = 0 }

/* EMC2301 data structure */
struct emc2301_data {
	uint8_t pwm_duty;           /* Current PWM duty cycle (0-255) */
	uint16_t fan_rpm;           /* Current fan RPM (0 = stopped/absent) */
	uint16_t tach_count;        /* 13-bit tach COUNT (0x1FFF = no signal) */
	bool tach_valid;            /* COUNT is a real measurement */
	uint8_t status_reg;         /* Raw 0x24 contents (read-to-clear) */
	bool fan_fault;             /* stall || drive_fail (legacy summary flag) */
	bool stall;                 /* FNSTL */
	bool spin_fail;             /* FNSPIN */
	bool drive_fail;            /* DVFAIL */
	bool watchdog;              /* WATCH */
	bool initialized;           /* Driver initialization status */
};

/**
 * Initialize EMC2301 fan controller with the given hardware configuration.
 * @return 0 on success, negative error code otherwise
 */
int emc2301_init(const struct emc2301_config *cfg);

/** True once emc2301_init() succeeded. */
bool emc2301_is_initialized(void);

/**
 * Write the PWM/tach configuration to the chip (0x2A, 0x2B, 0x2D, 0x31, 0x32,
 * 0x38) and verify it by read-back. Values are snapped to what the chip supports.
 * @return 0 on success, -EINVAL on bad values, -EIO on read-back mismatch
 */
int emc2301_apply_config(const struct emc2301_config *cfg);

/** Copy of the configuration currently applied to the chip (snapped values). */
void emc2301_get_config(struct emc2301_config *out);

/** Nearest supported PWM base frequency for @p hz (26000/19531/4882/2441). */
uint32_t emc2301_snap_pwm_base_hz(uint32_t hz);

/** Effective PWM output frequency (base / divide) of the applied configuration. */
uint32_t emc2301_effective_pwm_hz(void);

/**
 * Convert a 13-bit tach COUNT to RPM under configuration @p cfg
 * (edges sampled, pulses per revolution, range multiplier):
 *
 *     RPM = (edges - 1) * 1966080 * m / (pulses_per_rev * COUNT)
 *
 * which is the datasheet equation with poles = pulses per revolution. Sampling
 * fewer edges than 2*ppr+1 shortens the measurement window, so slower fans can
 * be read (the floor drops proportionally) at the cost of resolution.
 * Returns 0 for a stopped/absent fan (COUNT saturated) or an invalid count.
 */
uint16_t emc2301_count_to_rpm(uint16_t count13, const struct emc2301_config *cfg);

/** Lowest RPM the chip can report under @p cfg (COUNT saturates below it). */
uint16_t emc2301_config_min_rpm(const struct emc2301_config *cfg);

/** Edges actually sampled under @p cfg (explicit, or 2*ppr+1 when auto). */
uint8_t emc2301_config_edges(const struct emc2301_config *cfg);

/**
 * Read the raw tachometer COUNT. *valid is false when the fan is stopped or
 * absent (count saturated at 0x1FFF).
 */
int emc2301_read_tach(uint16_t *count13, bool *valid);

/**
 * Set PWM duty cycle (0-255)
 * @param duty PWM duty cycle value (0=off, 255=full speed)
 * @return 0 on success, negative error code otherwise
 */
int emc2301_set_pwm_duty(uint8_t duty);

/**
 * Get current PWM duty cycle
 * @param duty Pointer to store current duty cycle
 * @return 0 on success, negative error code otherwise
 */
int emc2301_get_pwm_duty(uint8_t *duty);

/**
 * Get current fan speed in RPM (0 when stopped/absent)
 * @param rpm Pointer to store current RPM
 * @return 0 on success, negative error code otherwise
 */
int emc2301_get_fan_speed(uint16_t *rpm);

/**
 * Get fan status information
 * @param data Pointer to emc2301_data structure to fill
 * @return 0 on success, negative error code otherwise
 */
int emc2301_get_status(struct emc2301_data *data);

/** Raw register read (diagnostics: `tank fan regs`). */
int emc2301_read_reg(uint8_t reg, uint8_t *val);

/** Edges sampled for a given pulses-per-revolution value. */
static inline uint8_t emc2301_edges_for_ppr(uint8_t ppr)
{
	return (uint8_t)(2u * ppr + 1u);
}

/** Minimum reportable RPM for a range multiplier. */
static inline uint16_t emc2301_range_min_rpm(uint8_t range_mult)
{
	return (uint16_t)(EMC2301_TACH_RANGE_BASE_RPM * range_mult);
}

/**
 * Convert duty cycle to percentage
 * @param duty PWM duty cycle (0-255)
 * @return percentage (0-100)
 */
static inline uint8_t emc2301_duty_to_percent(uint8_t duty)
{
	return (uint8_t)((duty * 100u + 127u) / 255u);
}

/**
 * Convert percentage to duty cycle (100 % -> 255, never wraps to 0)
 * @param percent Percentage (0-100, clamped)
 * @return PWM duty cycle (0-255)
 */
static inline uint8_t emc2301_percent_to_duty(uint8_t percent)
{
	if (percent > 100u) {
		percent = 100u;
	}
	return (uint8_t)((percent * 255u + 50u) / 100u);
}

#endif /* EMC2301_H */
