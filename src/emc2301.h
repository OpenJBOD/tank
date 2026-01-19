/*
 * EMC2301 Fan Controller Driver for OpenJBOD
 * Based on reference implementation and datasheet
 */

#ifndef EMC2301_H
#define EMC2301_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* EMC2301 I2C Address */
#define EMC2301_I2C_ADDR           0x2F

/* EMC2301 Register Definitions */
#define EMC2301_REG_CONFIG         0x20  /* Configuration register */
#define EMC2301_REG_FAN_STATUS     0x24  /* Fan status register */
#define EMC2301_REG_FAN_STALL      0x25  /* Fan stall status */
#define EMC2301_REG_FAN_SPIN       0x26  /* Fan spin status */
#define EMC2301_REG_DRIVE_FAIL     0x27  /* Drive fail status */
#define EMC2301_REG_FAN_INT_EN     0x29  /* Fan interrupt enable */
#define EMC2301_REG_PWM_POLARITY   0x2A  /* PWM polarity */
#define EMC2301_REG_PWM_OUT_CONFIG 0x2B  /* PWM output configuration */
#define EMC2301_REG_PWM_BASE_FREQ  0x2D  /* PWM base frequency */
#define EMC2301_REG_FAN_DRIVE      0x30  /* Fan drive (PWM duty cycle) */
#define EMC2301_REG_PWM_DIVIDE     0x31  /* PWM frequency divide */
#define EMC2301_REG_FAN_CONFIG1    0x32  /* Fan configuration 1 */
#define EMC2301_REG_FAN_CONFIG2    0x33  /* Fan configuration 2 */
#define EMC2301_REG_PID_GAIN       0x35  /* PID gain settings */
#define EMC2301_REG_FAN_SPIN_UP    0x36  /* Fan spin up configuration */
#define EMC2301_REG_MAX_STEP       0x37  /* Maximum step register */
#define EMC2301_REG_MIN_DRIVE      0x38  /* Minimum drive setting */
#define EMC2301_REG_VALID_TACH     0x39  /* Valid tach count */
#define EMC2301_REG_DRIVE_FAIL_LSB 0x3A  /* Drive fail band low byte */
#define EMC2301_REG_DRIVE_FAIL_MSB 0x3B  /* Drive fail band high byte */
#define EMC2301_REG_TACH_TARGET_LSB 0x3C /* Tach target low byte */
#define EMC2301_REG_TACH_TARGET_MSB 0x3D /* Tach target high byte */
#define EMC2301_REG_TACH_READING_MSB 0x3E /* Tach reading high byte */
#define EMC2301_REG_TACH_READING_LSB 0x3F /* Tach reading low byte */
#define EMC2301_REG_SOFTWARE_LOCK  0xEF  /* Software lock register */
#define EMC2301_REG_PRODUCT_ID     0xFD  /* Product ID register */
#define EMC2301_REG_MANUFACTURER   0xFE  /* Manufacturer ID register */
#define EMC2301_REG_REVISION       0xFF  /* Revision register */

/* EMC2301 Constants */
#define EMC2301_MFG_ID             0x5D  /* Expected manufacturer ID */
#define EMC2301_PRODUCT_ID         0x37  /* Expected product ID for EMC2301 */
#define EMC2301_FAN_MAX            255   /* Maximum PWM duty cycle value */
#define EMC2301_FAN_MIN            0     /* Minimum PWM duty cycle value */
#define EMC2301_RPM_FACTOR         3932160  /* RPM calculation factor */
#define EMC2301_TACH_REGS_UNUSE_BITS 3   /* Unused bits in tach registers */
#define EMC2301_TACH_CNT_MULTIPLIER  2   /* Tach count multiplier */
#define EMC2301_TACH_RANGE_MIN     480   /* Minimum tach range */

/* Configuration register bits */
#define EMC2301_CONFIG_MASK        0x80
#define EMC2301_CONFIG_DIS_TO      0x40
#define EMC2301_CONFIG_WD_EN       0x20
#define EMC2301_CONFIG_DRV_EN      0x02
#define EMC2301_CONFIG_USE_CLK     0x01

/* Fan status register bits */
#define EMC2301_FAN_STATUS_WATCH   0x80
#define EMC2301_FAN_STATUS_DVFAIL  0x04
#define EMC2301_FAN_STATUS_FNSPIN  0x02
#define EMC2301_FAN_STATUS_FNSTL   0x01

/* Fan configuration register bits */
#define EMC2301_FAN_CFG1_EDGES_MASK 0x60  /* Bits 6:5 for edges */
#define EMC2301_FAN_CFG1_RANGE_MASK 0x18  /* Bits 4:3 for range */
#define EMC2301_FAN_CFG1_UPDATE     0x04  /* Update tach */
#define EMC2301_FAN_CFG1_ENABLE     0x80  /* Enable fan algorithm */

/* Fan edges definitions */
#define EMC2301_FAN_EDGES_3        0x00  /* 3 edges (1 pole) */
#define EMC2301_FAN_EDGES_5        0x20  /* 5 edges (2 poles) */
#define EMC2301_FAN_EDGES_7        0x40  /* 7 edges (3 poles) */
#define EMC2301_FAN_EDGES_9        0x60  /* 9 edges (4 poles) */

/* Fan range definitions (multiplier for minimum RPM) */
#define EMC2301_FAN_RANGE_500      0x00  /* 500 RPM minimum */
#define EMC2301_FAN_RANGE_1000     0x08  /* 1000 RPM minimum */
#define EMC2301_FAN_RANGE_2000     0x10  /* 2000 RPM minimum */
#define EMC2301_FAN_RANGE_4000     0x18  /* 4000 RPM minimum */

/* EMC2301 data structure */
struct emc2301_data {
	uint8_t pwm_duty;           /* Current PWM duty cycle (0-255) */
	uint16_t fan_rpm;           /* Current fan RPM */
	bool fan_fault;             /* Fan fault status */
	bool initialized;           /* Driver initialization status */
};

/**
 * Initialize EMC2301 fan controller
 * @return 0 on success, negative error code otherwise
 */
int emc2301_init(void);

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
 * Get current fan speed in RPM
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

/**
 * Set fan configuration (edges and range)
 * @param edges Number of edges (3, 5, 7, or 9)
 * @param range_multiplier Range multiplier (1, 2, 4, or 8)
 * @return 0 on success, negative error code otherwise
 */
int emc2301_set_fan_config(uint8_t edges, uint8_t range_multiplier);

/**
 * Convert duty cycle to percentage
 * @param duty PWM duty cycle (0-255)
 * @return percentage (0-100)
 */
static inline uint8_t emc2301_duty_to_percent(uint8_t duty)
{
	return (duty * 100 + 128) / 256;
}

/**
 * Convert percentage to duty cycle
 * @param percent Percentage (0-100)
 * @return PWM duty cycle (0-255)
 */
static inline uint8_t emc2301_percent_to_duty(uint8_t percent)
{
	return (percent * 256 + 50) / 100;
}

#endif /* EMC2301_H */