#include "ds130x.h"


#include "gdefines.h"
#include "gconfig.h"

#include "gutils.h"
#include "gsystem.h"


#if defined(GSYSTEM_DS130X_CLOCK)


#ifdef GSYSTEM_DS1302_CLOCK
#   define CLOCK_DELAY_US (2)

static util_port_pin_t pin_clk = {GSYSTEM_CLOCK_CLK};
static util_port_pin_t pin_io = {GSYSTEM_CLOCK_IO};
static util_port_pin_t pin_ce = {GSYSTEM_CLOCK_CE};
#endif

/**
 * @brief Initializes the DS130X module. Sets clock halt bit to 0 to start timing.
 * @param hi2c User I2C handle pointer.
 */
DS130X_STATUS DS130X_Init() {
#ifdef GSYSTEM_DS1302_CLOCK
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = pin_ce.pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pin_ce.port, &GPIO_InitStruct);
    memset((void*)&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
    GPIO_InitStruct.Pin   = pin_clk.pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pin_clk.port, &GPIO_InitStruct);
    memset((void*)&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));
    GPIO_InitStruct.Pin  = pin_io.pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(pin_io.port, &GPIO_InitStruct);
	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(pin_io.port, pin_io.pin, GPIO_PIN_RESET);
#endif
	if (DS130X_SetClockHalt(0) != DS130X_OK) {
		return DS130X_ERROR;
	}
#ifdef GSYSTEM_DS1307_CLOCK
	if (DS130X_SetTimeZone(+0, 00) != DS130X_OK) {
		return DS130X_ERROR;
	}
#endif
	return DS130X_OK;
}

/**
 * @brief Sets clock halt bit.
 * @param halt Clock halt bit to set, 0 or 1. 0 to start timing, 0 to stop.
 */
DS130X_STATUS DS130X_SetClockHalt(uint8_t halt) {
	uint8_t ch = (halt ? 1 << 7 : 0);
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_SECOND, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	if (DS130X_SetReg((uint8_t)DS130X_REG_SECOND, ch | (val & 0x7f)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Gets clock halt bit.
 * @return Clock halt bit, 0 or 1.
 */
DS130X_STATUS DS130X_GetClockHalt(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg(DS130X_REG_SECOND, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = (val & 0x80) >> 7;
	return DS130X_OK;
}


#ifdef GSYSTEM_DS1302_CLOCK
static void DS1302_WriteBit(uint8_t bit)
{
	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_RESET);
	system_delay_us(CLOCK_DELAY_US);
	HAL_GPIO_WritePin(pin_io.port, pin_io.pin, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
	system_delay_us(CLOCK_DELAY_US);
	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_SET);
	system_delay_us(CLOCK_DELAY_US);
}
static void DS1302_WriteByte(uint8_t data)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = pin_io.pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(pin_io.port, &GPIO_InitStruct);
    for(uint8_t i = 0; i < 8; i++) {
        DS1302_WriteBit(data & 0x01);
        data >>= 1;
    }
	system_delay_us(CLOCK_DELAY_US);
}
static uint8_t DS1302_ReadBit(void)
{
    HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_RESET);
	system_delay_us(CLOCK_DELAY_US);
    uint8_t bit = (HAL_GPIO_ReadPin(pin_io.port, pin_io.pin) == GPIO_PIN_SET);
	system_delay_us(CLOCK_DELAY_US);
    HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_SET);
	system_delay_us(CLOCK_DELAY_US);
    return bit;
}
static uint8_t DS1302_ReadByte(void)
{
    uint8_t data = 0;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin  = pin_io.pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(pin_io.port, &GPIO_InitStruct);
    for(uint8_t i = 0; i < 8; i++) {
        if (DS1302_ReadBit()) {
        	data |= (0x01 << i);
        }
    }
	system_delay_us(CLOCK_DELAY_US);
    return data;
}
static void DS130X_SetRegByte(uint8_t regAddr, uint8_t val)
{
	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_RESET);
	system_delay_us(CLOCK_DELAY_US);

	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_SET);

	DS1302_WriteByte(regAddr & 0xFE);
	DS1302_WriteByte(val);

	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_RESET);
}
static void DS130X_GetRegByte(uint8_t regAddr, uint8_t* val)
{
	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_RESET);
	system_delay_us(CLOCK_DELAY_US);

	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_SET);

	DS1302_WriteByte(regAddr | 0x01);
	*val = DS1302_ReadByte();

	HAL_GPIO_WritePin(pin_clk.port, pin_clk.pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(pin_ce.port, pin_ce.pin, GPIO_PIN_RESET);
}
#endif
/**
 * @brief Sets the byte in the designated DS130X register to value.
 * @param regAddr Register address to write.
 * @param val Value to set, 0 to 255.
 */
DS130X_STATUS DS130X_SetReg(uint8_t regAddr, uint8_t val) {
	DS130X_STATUS status = DS130X_OK;
#ifdef GSYSTEM_DS1302_CLOCK
	DS130X_SetRegByte(DS130X_REG_WP, 0x00);
	DS130X_SetRegByte(regAddr, val);
	DS130X_SetRegByte(DS130X_REG_WP, 0x80);
#elif defined(GSYSTEM_DS1307_CLOCK)
	uint8_t bytes[2] = { regAddr, val };
	status = HAL_I2C_Master_Transmit(
		&GSYSTEM_CLOCK_I2C,
		DS130X_I2C_ADDR << 1,
		bytes,
		2,
		DS130X_TIMEOUT
	) == HAL_OK ? DS130X_OK : DS130X_ERROR;
#endif
	return status;
}

/**
 * @brief Gets the byte in the designated DS130X register.
 * @param regAddr Register address to read.
 * @return Value stored in the register, 0 to 255.
 */
DS130X_STATUS DS130X_GetReg(uint8_t regAddr, uint8_t* res) {
	*res = 0;
	DS130X_STATUS status = DS130X_OK;
	uint8_t val = 0;
#ifdef GSYSTEM_DS1302_CLOCK
	DS130X_GetRegByte(regAddr, &val);
	goto do_success;
#elif defined(GSYSTEM_DS1307_CLOCK)
	if (HAL_I2C_Master_Transmit(&GSYSTEM_CLOCK_I2C, DS130X_I2C_ADDR << 1, &regAddr, 1, DS130X_TIMEOUT) != HAL_OK) {
		goto do_error;
	}
	if (HAL_I2C_Master_Receive(&GSYSTEM_CLOCK_I2C, DS130X_I2C_ADDR << 1, (uint8_t*)&val, 1, DS130X_TIMEOUT) != HAL_OK) {
		goto do_error;
	}
	goto do_success;
do_error:
	status = DS130X_ERROR;
	goto do_end;
#endif
do_success:
	*res = val;
	goto do_end;
do_end:
	return status;
}

DS130X_STATUS DS130X_SetRAM(uint8_t index, uint8_t val) {
#ifdef GSYSTEM_DS1302_CLOCK
	index *= 2;
#endif
	if (index > DS130X_REG_RAM_END) {
		return DS130X_ERROR;
	}
	return DS130X_SetReg(DS130X_REG_RAM_BEGIN + index, val);
}

DS130X_STATUS DS130X_GetRAM(uint8_t index, uint8_t* val) {
#ifdef GSYSTEM_DS1302_CLOCK
	index *= 2;
#endif
	if (index > DS130X_REG_RAM_END) {
		return DS130X_ERROR;
	}
	return DS130X_GetReg(DS130X_REG_RAM_BEGIN + index, val);
}

#ifdef GSYSTEM_DS1307_CLOCK
/**
 * @brief Toggle square wave output on pin 7.
 * @param mode DS130X_ENABLED (1) or DS130X_DISABLED (0);
 */
DS130X_STATUS DS130X_SetEnableSquareWave(DS130X_SquareWaveEnable mode){
	uint8_t controlReg = 0;
	if (DS130X_GetReg(DS130X_REG_CONTROL, &controlReg) != DS130X_OK) {
		return DS130X_ERROR;
	}
	uint8_t newControlReg = (uint8_t)(controlReg & ~(1 << 4)) | ((mode & 1) << 4);
	if (DS130X_SetReg((uint8_t)DS130X_REG_CONTROL, newControlReg) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Set square wave output frequency.
 * @param rate DS130X_1HZ (0b00), DS130X_4096HZ (0b01), DS130X_8192HZ (0b10) or DS130X_32768HZ (0b11).
 */
DS130X_STATUS DS130X_SetInterruptRate(DS130X_Rate rate){
	uint8_t controlReg = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_CONTROL, &controlReg) != DS130X_OK) {
		return DS130X_ERROR;
	}
	uint8_t newControlReg = (uint8_t)(controlReg & ~0x03) | rate;
	if (DS130X_SetReg((uint8_t)DS130X_REG_CONTROL, newControlReg) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}
#endif

/**
 * @brief Gets the current day of week.
 * @return Days from last Sunday, 0 to 6.
 */
DS130X_STATUS DS130X_GetDayOfWeek(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_DOW, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
#ifdef GSYSTEM_DS1302_CLOCK
	val &= 0x07;
#endif
	*res = DS130X_DecodeBCD(val);
	return DS130X_OK;
}

/**
 * @brief Gets the current day of month.
 * @return Day of month, 1 to 31.
 */
DS130X_STATUS DS130X_GetDate(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_DATE, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = DS130X_DecodeBCD(val);
	return DS130X_OK;
}

/**
 * @brief Gets the current month.
 * @return Month, 1 to 12.
 */
DS130X_STATUS DS130X_GetMonth(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_MONTH, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = DS130X_DecodeBCD(val);
	return DS130X_OK;
}

/**
 * @brief Gets the current year.
 * @return Year, 2000 to 2099.
 */
DS130X_STATUS DS130X_GetYear(uint16_t* res) {
	*res = 0;
	uint8_t val = 0;
#ifdef GSYSTEM_DS1307_CLOCK
	if (DS130X_GetReg((uint8_t)DS130X_REG_RAM_CENT, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	uint16_t cen = (uint16_t)val * 100;
#endif
	val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_YEAR, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
#ifdef GSYSTEM_DS1307_CLOCK
	*res = (uint16_t)DS130X_DecodeBCD(val) + cen;
#else
	*res = ((uint16_t)DS130X_DecodeBCD(val)) % 100;
#endif
	return DS130X_OK;
}

/**
 * @brief Gets the current hour in 24h format.
 * @return Hour in 24h format, 0 to 23.
 */
DS130X_STATUS DS130X_GetHour(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_HOUR, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = DS130X_DecodeBCD(val & 0x3f);
	return DS130X_OK;
}

/**
 * @brief Gets the current minute.
 * @return Minute, 0 to 59.
 */
DS130X_STATUS DS130X_GetMinute(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_MINUTE, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = DS130X_DecodeBCD(val);
	return DS130X_OK;
}

/**
 * @brief Gets the current second. Clock halt bit not included.
 * @return Second, 0 to 59.
 */
DS130X_STATUS DS130X_GetSecond(uint8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_SECOND, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = DS130X_DecodeBCD(val & 0x7f);
	return DS130X_OK;
}

#ifdef GSYSTEM_DS1307_CLOCK
/**
 * @brief Gets the stored UTC hour offset.
 * @note  UTC offset is not updated automatically.
 * @return UTC hour offset, -12 to 12.
 */
DS130X_STATUS DS130X_GetTimeZoneHour(int8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_RAM_UTC_HR, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = (int8_t)val;
	return DS130X_OK;
}

/**
 * @brief Gets the stored UTC minute offset.
 * @note  UTC offset is not updated automatically.
 * @return UTC time zone, 0 to 59.
 */
DS130X_STATUS DS130X_GetTimeZoneMin(int8_t* res) {
	*res = 0;
	uint8_t val = 0;
	if (DS130X_GetReg((uint8_t)DS130X_REG_RAM_UTC_MIN, &val) != DS130X_OK) {
		return DS130X_ERROR;
	}
	*res = (int8_t)val;
	return DS130X_OK;
}
#endif

/**
 * @brief Sets the current day of week.
 * @param dayOfWeek Days since last Sunday, 0 to 6.
 */
DS130X_STATUS DS130X_SetDayOfWeek(uint8_t dayOfWeek) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_DOW, DS130X_EncodeBCD(dayOfWeek)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current day of month.
 * @param date Day of month, 1 to 31.
 */
DS130X_STATUS DS130X_SetDate(uint8_t date) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_DATE, DS130X_EncodeBCD(date)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current month.
 * @param month Month, 1 to 12.
 */
DS130X_STATUS DS130X_SetMonth(uint8_t month) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_MONTH, DS130X_EncodeBCD(month)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current year.
 * @param year Year, 2000 to 2099.
 */
DS130X_STATUS DS130X_SetYear(uint16_t year) {
#ifdef GSYSTEM_DS1307_CLOCK
	if (DS130X_SetReg((uint8_t)DS130X_REG_RAM_CENT, (uint8_t)(year / 100)) != DS130X_OK) {
		return DS130X_ERROR;
	}
#endif
	if (DS130X_SetReg((uint8_t)DS130X_REG_YEAR, DS130X_EncodeBCD((uint8_t)(year % 100))) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current hour, in 24h format.
 * @param hour_24mode Hour in 24h format, 0 to 23.
 */
DS130X_STATUS DS130X_SetHour(uint8_t hour_24mode) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_HOUR, DS130X_EncodeBCD((uint8_t)hour_24mode & 0x3f)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current minute.
 * @param minute Minute, 0 to 59.
 */
DS130X_STATUS DS130X_SetMinute(uint8_t minute) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_MINUTE, DS130X_EncodeBCD((uint8_t)minute)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

/**
 * @brief Sets the current second.
 * @param second Second, 0 to 59.
 */
DS130X_STATUS DS130X_SetSecond(uint8_t second) {
	uint8_t ch = 0;
	if (DS130X_GetClockHalt(&ch) != DS130X_OK) {
		return DS130X_ERROR;
	}
	if (DS130X_SetReg((uint8_t)DS130X_REG_SECOND, DS130X_EncodeBCD(second | ch)) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}

#ifdef GSYSTEM_DS1307_CLOCK
/**
 * @brief Sets UTC offset.
 * @note  UTC offset is not updated automatically.
 * @param hr UTC hour offset, -12 to 12.
 * @param min UTC minute offset, 0 to 59.
 */
DS130X_STATUS DS130X_SetTimeZone(int8_t hr, uint8_t min) {
	if (DS130X_SetReg((uint8_t)DS130X_REG_RAM_UTC_HR, (uint8_t)hr) != DS130X_OK) {
		return DS130X_ERROR;
	}
	if (DS130X_SetReg((uint8_t)DS130X_REG_RAM_UTC_MIN, (uint8_t)min) != DS130X_OK) {
		return DS130X_ERROR;
	}
	return DS130X_OK;
}
#endif

/**
 * @brief Decodes the raw binary value stored in registers to decimal format.
 * @param bin Binary-coded decimal value retrieved from register, 0 to 255.
 * @return Decoded decimal value.
 */
uint8_t DS130X_DecodeBCD(uint8_t bin) {
	return (uint8_t)(((bin & 0xf0) >> 4) * 10) + (bin & 0x0f);
}

/**
 * @brief Encodes a decimal number to binaty-coded decimal for storage in registers.
 * @param dec Decimal number to encode.
 * @return Encoded binary-coded decimal value.
 */
uint8_t DS130X_EncodeBCD(uint8_t dec) {
	return (uint8_t)(dec % 10 + ((dec / 10) << 4));
}


#endif
