#include "ds1307.h"


#include "gdefines.h"
#include "gconfig.h"


#if defined(GSYSTEM_DS1307_CLOCK)


/**
 * @brief Initializes the DS1307 module. Sets clock halt bit to 0 to start timing.
 * @param hi2c User I2C handle pointer.
 */
DS1307_STATUS DS1307_Init() {
	if (DS1307_SetClockHalt(0) != DS1307_OK) {
		return DS1307_ERROR;
	}
	if (DS1307_SetTimeZone(+0, 00) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets clock halt bit.
 * @param halt Clock halt bit to set, 0 or 1. 0 to start timing, 0 to stop.
 */
DS1307_STATUS DS1307_SetClockHalt(uint8_t halt) {
	uint8_t ch = (halt ? 1 << 7 : 0);
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_SECOND, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_SECOND, ch | (val & 0x7f)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Gets clock halt bit.
 * @return Clock halt bit, 0 or 1.
 */
DS1307_STATUS DS1307_GetClockHalt(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte(DS1307_REG_SECOND, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = (val & 0x80) >> 7;
	return DS1307_OK;
}

/**
 * @brief Sets the byte in the designated DS1307 register to value.
 * @param regAddr Register address to write.
 * @param val Value to set, 0 to 255.
 */
DS1307_STATUS DS1307_SetRegByte(uint8_t regAddr, uint8_t val) {
	uint8_t bytes[2] = { regAddr, val };
	return HAL_I2C_Master_Transmit(&GSYSTEM_CLOCK_I2C, DS1307_I2C_ADDR << 1, bytes, 2, DS1307_TIMEOUT) == HAL_OK ?
			DS1307_OK : DS1307_ERROR;
}

/**
 * @brief Gets the byte in the designated DS1307 register.
 * @param regAddr Register address to read.
 * @return Value stored in the register, 0 to 255.
 */
DS1307_STATUS DS1307_GetRegByte(uint8_t regAddr, uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (HAL_I2C_Master_Transmit(&GSYSTEM_CLOCK_I2C, DS1307_I2C_ADDR << 1, &regAddr, 1, DS1307_TIMEOUT) != HAL_OK) {
		return DS1307_ERROR;
	}
	if (HAL_I2C_Master_Receive(&GSYSTEM_CLOCK_I2C, DS1307_I2C_ADDR << 1, (uint8_t*)&val, 1, DS1307_TIMEOUT) != HAL_OK) {
		return DS1307_ERROR;
	}
	*res = val;
	return DS1307_OK;
}

/**
 * @brief Toggle square wave output on pin 7.
 * @param mode DS1307_ENABLED (1) or DS1307_DISABLED (0);
 */
DS1307_STATUS DS1307_SetEnableSquareWave(DS1307_SquareWaveEnable mode){
	uint8_t controlReg = 0;
	if (DS1307_GetRegByte(DS1307_REG_CONTROL, &controlReg) != DS1307_OK) {
		return DS1307_ERROR;
	}
	uint8_t newControlReg = (uint8_t)(controlReg & ~(1 << 4)) | ((mode & 1) << 4);
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_CONTROL, newControlReg) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Set square wave output frequency.
 * @param rate DS1307_1HZ (0b00), DS1307_4096HZ (0b01), DS1307_8192HZ (0b10) or DS1307_32768HZ (0b11).
 */
DS1307_STATUS DS1307_SetInterruptRate(DS1307_Rate rate){
	uint8_t controlReg = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_CONTROL, &controlReg) != DS1307_OK) {
		return DS1307_ERROR;
	}
	uint8_t newControlReg = (uint8_t)(controlReg & ~0x03) | rate;
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_CONTROL, newControlReg) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Gets the current day of week.
 * @return Days from last Sunday, 0 to 6.
 */
DS1307_STATUS DS1307_GetDayOfWeek(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_DOW, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val);
	return DS1307_OK;
}

/**
 * @brief Gets the current day of month.
 * @return Day of month, 1 to 31.
 */
DS1307_STATUS DS1307_GetDate(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_DATE, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val);
	return DS1307_OK;
}

/**
 * @brief Gets the current month.
 * @return Month, 1 to 12.
 */
DS1307_STATUS DS1307_GetMonth(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_MONTH, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val);
	return DS1307_OK;
}

/**
 * @brief Gets the current year.
 * @return Year, 2000 to 2099.
 */
DS1307_STATUS DS1307_GetYear(uint16_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_RAM_CENT, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	uint16_t cen = (uint16_t)val * 100;
	val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_YEAR, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = (uint16_t)DS1307_DecodeBCD(val) + cen;
	return DS1307_OK;
}

/**
 * @brief Gets the current hour in 24h format.
 * @return Hour in 24h format, 0 to 23.
 */
DS1307_STATUS DS1307_GetHour(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_HOUR, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val & 0x3f);
	return DS1307_OK;
}

/**
 * @brief Gets the current minute.
 * @return Minute, 0 to 59.
 */
DS1307_STATUS DS1307_GetMinute(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_MINUTE, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val);
	return DS1307_OK;
}

/**
 * @brief Gets the current second. Clock halt bit not included.
 * @return Second, 0 to 59.
 */
DS1307_STATUS DS1307_GetSecond(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_SECOND, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = DS1307_DecodeBCD(val & 0x7f);
	return DS1307_OK;
}

/**
 * @brief Gets the stored UTC hour offset.
 * @note  UTC offset is not updated automatically.
 * @return UTC hour offset, -12 to 12.
 */
DS1307_STATUS DS1307_GetTimeZoneHour(int8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_RAM_UTC_HR, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = (int8_t)val;
	return DS1307_OK;
}

/**
 * @brief Gets the stored UTC minute offset.
 * @note  UTC offset is not updated automatically.
 * @return UTC time zone, 0 to 59.
 */
DS1307_STATUS DS1307_GetTimeZoneMin(int8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS1307_GetRegByte((uint8_t)DS1307_REG_RAM_UTC_MIN, &val) != DS1307_OK) {
		return DS1307_ERROR;
	}
	*res = (int8_t)val;
	return DS1307_OK;
}

/**
 * @brief Sets the current day of week.
 * @param dayOfWeek Days since last Sunday, 0 to 6.
 */
DS1307_STATUS DS1307_SetDayOfWeek(uint8_t dayOfWeek) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_DOW, DS1307_EncodeBCD(dayOfWeek)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current day of month.
 * @param date Day of month, 1 to 31.
 */
DS1307_STATUS DS1307_SetDate(uint8_t date) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_DATE, DS1307_EncodeBCD(date)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current month.
 * @param month Month, 1 to 12.
 */
DS1307_STATUS DS1307_SetMonth(uint8_t month) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_MONTH, DS1307_EncodeBCD(month)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current year.
 * @param year Year, 2000 to 2099.
 */
DS1307_STATUS DS1307_SetYear(uint16_t year) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_RAM_CENT, (uint8_t)(year / 100)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_YEAR, DS1307_EncodeBCD((uint8_t)(year % 100))) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current hour, in 24h format.
 * @param hour_24mode Hour in 24h format, 0 to 23.
 */
DS1307_STATUS DS1307_SetHour(uint8_t hour_24mode) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_HOUR, DS1307_EncodeBCD((uint8_t)hour_24mode & 0x3f)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current minute.
 * @param minute Minute, 0 to 59.
 */
DS1307_STATUS DS1307_SetMinute(uint8_t minute) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_MINUTE, DS1307_EncodeBCD((uint8_t)minute)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets the current second.
 * @param second Second, 0 to 59.
 */
DS1307_STATUS DS1307_SetSecond(uint8_t second) {
	uint8_t ch = 0;
	if (DS1307_GetClockHalt(&ch) != DS1307_OK) {
		return DS1307_ERROR;
	}
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_SECOND, DS1307_EncodeBCD(second | ch)) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Sets UTC offset.
 * @note  UTC offset is not updated automatically.
 * @param hr UTC hour offset, -12 to 12.
 * @param min UTC minute offset, 0 to 59.
 */
DS1307_STATUS DS1307_SetTimeZone(int8_t hr, uint8_t min) {
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_RAM_UTC_HR, (uint8_t)hr) != DS1307_OK) {
		return DS1307_ERROR;
	}
	if (DS1307_SetRegByte((uint8_t)DS1307_REG_RAM_UTC_MIN, (uint8_t)min) != DS1307_OK) {
		return DS1307_ERROR;
	}
	return DS1307_OK;
}

/**
 * @brief Decodes the raw binary value stored in registers to decimal format.
 * @param bin Binary-coded decimal value retrieved from register, 0 to 255.
 * @return Decoded decimal value.
 */
uint8_t DS1307_DecodeBCD(uint8_t bin) {
	return (uint8_t)(((bin & 0xf0) >> 4) * 10) + (bin & 0x0f);
}

/**
 * @brief Encodes a decimal number to binaty-coded decimal for storage in registers.
 * @param dec Decimal number to encode.
 * @return Encoded binary-coded decimal value.
 */
uint8_t DS1307_EncodeBCD(uint8_t dec) {
	return (uint8_t)(dec % 10 + ((dec / 10) << 4));
}


#endif
