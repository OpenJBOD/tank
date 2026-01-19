/*
 * EMC2301 Fan Controller Driver for OpenJBOD
 * Based on reference implementation and datasheet
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include "emc2301.h"

LOG_MODULE_REGISTER(emc2301, LOG_LEVEL_INF);

/* Device tree reference for I2C bus */
static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* Driver data */
static struct emc2301_data emc_data = {0};

/**
 * Read a single byte from EMC2301 register
 */
static int emc2301_reg_read(uint8_t reg, uint8_t *value)
{
	int ret;
	int retries = 3;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	while (retries-- > 0) {
		ret = i2c_reg_read_byte(i2c_dev, EMC2301_I2C_ADDR, reg, value);
		if (ret >= 0) {
			LOG_DBG("I2C read: reg=0x%02x, value=0x%02x", reg, *value);
			return ret;
		}
		
		if (retries > 0) {
			LOG_WRN("I2C read retry: reg=0x%02x, ret=%d, retries left=%d", reg, ret, retries);
			k_msleep(10);  /* Small delay before retry */
		}
	}
	
	LOG_ERR("I2C read failed: reg=0x%02x, ret=%d", reg, ret);
	return ret;
}

/**
 * Write a single byte to EMC2301 register
 */
static int emc2301_reg_write(uint8_t reg, uint8_t value)
{
	int ret;
	int retries = 3;

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	while (retries-- > 0) {
		ret = i2c_reg_write_byte(i2c_dev, EMC2301_I2C_ADDR, reg, value);
		if (ret >= 0) {
			LOG_DBG("I2C write: reg=0x%02x, value=0x%02x", reg, value);
			return ret;
		}
		
		if (retries > 0) {
			LOG_WRN("I2C write retry: reg=0x%02x, value=0x%02x, ret=%d, retries left=%d", 
				reg, value, ret, retries);
			k_msleep(10);  /* Small delay before retry */
		}
	}
	
	LOG_ERR("I2C write failed: reg=0x%02x, value=0x%02x, ret=%d", reg, value, ret);
	return ret;
}

/**
 * Read two bytes from EMC2301 register (big-endian)
 */
static int emc2301_reg_read_word(uint8_t reg_msb, uint16_t *value)
{
	uint8_t msb, lsb;
	int ret;

	ret = emc2301_reg_read(reg_msb, &msb);
	if (ret < 0) {
		return ret;
	}

	ret = emc2301_reg_read(reg_msb + 1, &lsb);
	if (ret < 0) {
		return ret;
	}

	*value = (msb << 8) | lsb;
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

int emc2301_init(void)
{
	int ret;
	uint8_t config_val;

	LOG_INF("Initializing EMC2301 fan controller");

	if (!device_is_ready(i2c_dev)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	/* Verify device */
	ret = emc2301_verify_device();
	if (ret < 0) {
		return ret;
	}

	/* Step 1: Enable the device and drive output, disable watchdog */
	config_val = EMC2301_CONFIG_DRV_EN | EMC2301_CONFIG_DIS_TO;  /* Enable drive, disable timeout */
	LOG_INF("Writing EMC2301 configuration: 0x%02x", config_val);
	ret = emc2301_reg_write(EMC2301_REG_CONFIG, config_val);
	if (ret < 0) {
		LOG_ERR("Failed to write configuration register: %d", ret);
		return ret;
	}

	/* Read back configuration to verify */
	uint8_t config_readback;
	ret = emc2301_reg_read(EMC2301_REG_CONFIG, &config_readback);
	if (ret < 0) {
		LOG_WRN("Failed to read back configuration: %d", ret);
	} else {
		LOG_INF("EMC2301 configuration readback: 0x%02x", config_readback);
	}

	/* Step 2: Configure PWM output - set as push-pull output, not open-drain */
	ret = emc2301_reg_write(EMC2301_REG_PWM_OUT_CONFIG, 0x01);
	if (ret < 0) {
		LOG_WRN("Failed to configure PWM output: %d", ret);
	} else {
		LOG_INF("PWM output configured for push-pull mode");
	}

	/* Step 3: Set PWM base frequency to default (26 kHz) */
	ret = emc2301_reg_write(EMC2301_REG_PWM_BASE_FREQ, 0x1F);  /* Default frequency setting */
	if (ret < 0) {
		LOG_WRN("Failed to set PWM base frequency: %d", ret);
	} else {
		LOG_INF("PWM base frequency configured");
	}

	/* Step 4: Set PWM frequency divider to 1 (no division) */
	ret = emc2301_reg_write(EMC2301_REG_PWM_DIVIDE, 0x00);
	if (ret < 0) {
		LOG_WRN("Failed to set PWM frequency divider: %d", ret);
	} else {
		LOG_INF("PWM frequency divider configured");
	}

	/* Step 5: Set minimum drive to prevent fan stall - do this BEFORE setting PWM duty */
	ret = emc2301_reg_write(EMC2301_REG_MIN_DRIVE, emc2301_percent_to_duty(10));
	if (ret < 0) {
		LOG_WRN("Failed to set minimum drive: %d", ret);
	} else {
		LOG_INF("Minimum drive configured to %d%%", 10);
	}

	/* Step 6: Configure fan tachometer settings first */
	/* Try 3 edges (single pole) first - most common for PC fans */
	ret = emc2301_set_fan_config(3, 2);
	if (ret < 0) {
		LOG_WRN("Failed to set fan configuration: %d", ret);
		/* Continue initialization - this is not critical */
	}

	/* Step 7: Clear any existing faults */
	uint8_t stall_reg;
	ret = emc2301_reg_read(EMC2301_REG_FAN_STALL, &stall_reg);
	if (ret >= 0 && (stall_reg & 0x01)) {
		LOG_INF("Clearing fan stall status");
		ret = emc2301_reg_write(EMC2301_REG_FAN_STALL, stall_reg & ~0x01);
		if (ret < 0) {
			LOG_WRN("Failed to clear fan stall: %d", ret);
		}
	}

	/* Step 8: Set initial fan speed to 25% */
	ret = emc2301_set_pwm_duty(emc2301_percent_to_duty(25));
	if (ret < 0) {
		LOG_WRN("Failed to set initial fan speed: %d", ret);
	}

	emc_data.initialized = true;
	LOG_INF("EMC2301 initialized successfully");
	LOG_INF("Note: Fan faults are normal if no fan is physically connected");

	return 0;
}

int emc2301_set_pwm_duty(uint8_t duty)
{
	int ret;
	uint8_t readback;
	uint8_t config_reg;

	if (duty > EMC2301_FAN_MAX) {
		LOG_ERR("Invalid duty cycle: %d (max %d)", duty, EMC2301_FAN_MAX);
		return -EINVAL;
	}

	LOG_DBG("Setting PWM duty cycle to %d (%d%%)", duty, emc2301_duty_to_percent(duty));
	
	/* First, ensure the device is configured correctly for direct PWM control */
	ret = emc2301_reg_read(EMC2301_REG_CONFIG, &config_reg);
	if (ret < 0) {
		LOG_ERR("Failed to read config register: %d", ret);
		return ret;
	}
	
	/* Make sure DRV_EN is set */
	if (!(config_reg & EMC2301_CONFIG_DRV_EN)) {
		config_reg |= EMC2301_CONFIG_DRV_EN;
		ret = emc2301_reg_write(EMC2301_REG_CONFIG, config_reg);
		if (ret < 0) {
			LOG_ERR("Failed to enable drive output: %d", ret);
			return ret;
		}
		LOG_INF("Enabled drive output");
		/* Small delay to let the configuration take effect */
		k_msleep(10);
	}
	
	ret = emc2301_reg_write(EMC2301_REG_FAN_DRIVE, duty);
	if (ret < 0) {
		LOG_ERR("Failed to set PWM duty cycle: %d", ret);
		return ret;
	}

	/* Small delay before readback */
	k_msleep(1);

	/* Read back to verify */
	ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, &readback);
	if (ret < 0) {
		LOG_WRN("Failed to read back PWM duty: %d", ret);
	} else {
		LOG_DBG("PWM duty readback: %d (%d%%)", readback, emc2301_duty_to_percent(readback));
		if (readback != duty) {
			LOG_WRN("PWM duty mismatch: wrote %d, read %d", duty, readback);
			/* Try writing again if mismatch occurs */
			ret = emc2301_reg_write(EMC2301_REG_FAN_DRIVE, duty);
			if (ret < 0) {
				LOG_ERR("Retry failed to set PWM duty cycle: %d", ret);
				return ret;
			}
			k_msleep(1);
			ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, &readback);
			if (ret >= 0) {
				LOG_INF("Retry PWM duty readback: %d (%d%%)", readback, emc2301_duty_to_percent(readback));
			}
		}
	}

	emc_data.pwm_duty = duty;

	return 0;
}

int emc2301_get_pwm_duty(uint8_t *duty)
{
	int ret;

	if (duty == NULL) {
		return -EINVAL;
	}

	ret = emc2301_reg_read(EMC2301_REG_FAN_DRIVE, duty);
	if (ret < 0) {
		LOG_ERR("Failed to read PWM duty cycle: %d", ret);
		/* Return cached value if I2C fails */
		*duty = emc_data.pwm_duty;
		return 0;  /* Don't fail completely, just return cached value */
	}

	emc_data.pwm_duty = *duty;
	return 0;
}

int emc2301_get_fan_speed(uint16_t *rpm)
{
	uint16_t tach_count;
	uint8_t config1_reg, status_reg;
	int ret;

	if (rpm == NULL) {
		return -EINVAL;
	}

	ret = emc2301_reg_read_word(EMC2301_REG_TACH_READING_MSB, &tach_count);
	if (ret < 0) {
		LOG_ERR("Failed to read tachometer: %d", ret);
		/* Return cached value if I2C fails */
		*rpm = emc_data.fan_rpm;
		return 0;  /* Don't fail completely */
	}

	/* Read fan config1 and status for debugging */
	ret = emc2301_reg_read(EMC2301_REG_FAN_CONFIG1, &config1_reg);
	if (ret >= 0) {
		LOG_DBG("Fan config1: 0x%02x", config1_reg);
	}
	
	ret = emc2301_reg_read(EMC2301_REG_FAN_STATUS, &status_reg);
	if (ret >= 0) {
		LOG_DBG("Fan status: 0x%02x", status_reg);
	}

	LOG_DBG("Raw tach reading: 0x%04x", tach_count);

	/* Check for invalid reading (fan stopped or not connected) */
	if ((tach_count >> 8) == 0xFF || tach_count == 0) {
		*rpm = 0;
		emc_data.fan_rpm = 0;
		LOG_DBG("Invalid tach reading - fan may not be connected or not spinning");
		return 0;
	}

	/* Extract valid tach count (remove unused bits) */
	tach_count = tach_count >> EMC2301_TACH_REGS_UNUSE_BITS;

	if (tach_count == 0) {
		*rpm = 0;
	} else {
		/* Calculate RPM using the standard formula */
		uint32_t rpm_calc = EMC2301_RPM_FACTOR / tach_count;
		
		LOG_DBG("RPM calculation: %u / %u = %u", EMC2301_RPM_FACTOR, tach_count, rpm_calc);
		
		if (rpm_calc <= EMC2301_TACH_RANGE_MIN) {
			*rpm = 0;
		} else {
			*rpm = (uint16_t)(rpm_calc * EMC2301_TACH_CNT_MULTIPLIER);
		}
	}

	emc_data.fan_rpm = *rpm;
	LOG_DBG("Fan speed: %d RPM (processed tach: 0x%04x)", *rpm, tach_count);

	return 0;
}

int emc2301_get_status(struct emc2301_data *data)
{
	uint8_t status_reg;
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}

	/* Read current PWM duty */
	ret = emc2301_get_pwm_duty(&data->pwm_duty);
	if (ret < 0) {
		return ret;
	}

	/* Read current fan speed */
	ret = emc2301_get_fan_speed(&data->fan_rpm);
	if (ret < 0) {
		return ret;
	}

	/* Read fan status */
	ret = emc2301_reg_read(EMC2301_REG_FAN_STATUS, &status_reg);
	if (ret < 0) {
		LOG_ERR("Failed to read fan status: %d", ret);
		return ret;
	}

	/* Check for fan faults */
	data->fan_fault = (status_reg & (EMC2301_FAN_STATUS_FNSTL | 
					 EMC2301_FAN_STATUS_DVFAIL)) != 0;
	
	/* Log detailed fault information */
	if (status_reg & EMC2301_FAN_STATUS_FNSTL) {
		LOG_DBG("Fan fault: Fan stall detected");
	}
	if (status_reg & EMC2301_FAN_STATUS_DVFAIL) {
		LOG_DBG("Fan fault: Drive fail detected");
	}
	if (status_reg & EMC2301_FAN_STATUS_FNSPIN) {
		LOG_DBG("Fan status: Fan spin detected");
	}
	if (status_reg & EMC2301_FAN_STATUS_WATCH) {
		LOG_DBG("Fan status: Watchdog timeout");
	}
	
	data->initialized = emc_data.initialized;

	/* Update cached data */
	emc_data = *data;

	return 0;
}

int emc2301_set_fan_config(uint8_t edges, uint8_t range_multiplier)
{
	uint8_t config_val = 0;
	uint8_t edges_bits, range_bits;
	int ret;

	/* Map edges to register bits */
	switch (edges) {
	case 3:
		edges_bits = EMC2301_FAN_EDGES_3;
		break;
	case 5:
		edges_bits = EMC2301_FAN_EDGES_5;
		break;
	case 7:
		edges_bits = EMC2301_FAN_EDGES_7;
		break;
	case 9:
		edges_bits = EMC2301_FAN_EDGES_9;
		break;
	default:
		LOG_ERR("Invalid edge count: %d (must be 3, 5, 7, or 9)", edges);
		return -EINVAL;
	}

	/* Map range multiplier to register bits */
	switch (range_multiplier) {
	case 1:
		range_bits = EMC2301_FAN_RANGE_500;
		break;
	case 2:
		range_bits = EMC2301_FAN_RANGE_1000;
		break;
	case 4:
		range_bits = EMC2301_FAN_RANGE_2000;
		break;
	case 8:
		range_bits = EMC2301_FAN_RANGE_4000;
		break;
	default:
		LOG_ERR("Invalid range multiplier: %d (must be 1, 2, 4, or 8)", range_multiplier);
		return -EINVAL;
	}

	/* Read current configuration to preserve other bits */
	ret = emc2301_reg_read(EMC2301_REG_FAN_CONFIG1, &config_val);
	if (ret < 0) {
		LOG_WRN("Failed to read fan config, using default");
		config_val = 0;
	}
	LOG_INF("Current fan config1: 0x%02x", config_val);

	/* Clear and set edges and range bits, enable tach update but not fan algorithm */
	config_val &= ~(EMC2301_FAN_CFG1_EDGES_MASK | EMC2301_FAN_CFG1_RANGE_MASK | EMC2301_FAN_CFG1_ENABLE);
	config_val |= edges_bits | range_bits | EMC2301_FAN_CFG1_UPDATE;
	
	LOG_INF("Writing fan config1: 0x%02x (edges_bits=0x%02x, range_bits=0x%02x)", 
		config_val, edges_bits, range_bits);

	ret = emc2301_reg_write(EMC2301_REG_FAN_CONFIG1, config_val);
	if (ret < 0) {
		LOG_ERR("Failed to write fan configuration: %d", ret);
		return ret;
	}

	/* Read back to verify */
	uint8_t config1_readback;
	ret = emc2301_reg_read(EMC2301_REG_FAN_CONFIG1, &config1_readback);
	if (ret < 0) {
		LOG_WRN("Failed to read back fan config1: %d", ret);
	} else {
		LOG_INF("Fan config1 readback: 0x%02x", config1_readback);
	}

	LOG_INF("Fan configuration set: %d edges, %dx range multiplier", 
		edges, range_multiplier);

	return 0;
}