/* Copyright © 2025 Georgy E. All rights reserved. */

#ifndef _DS130X_H_
#define _DS130X_H_


#ifdef __cplusplus
extern "C"{
#endif


#include "gdefines.h"
#include "gconfig.h"


#if defined(GSYSTEM_DS130X_CLOCK)


#include <stdint.h>
#include "hal_defs.h"


typedef enum _DS130X_STATUS {
	DS130X_OK,
	DS130X_ERROR
} DS130X_STATUS;


/*----------------------------------------------------------------------------*/
#define DS130X_I2C_ADDR 	    0x68
#define DS130X_REG_SECOND 	    0x00
#define DS130X_REG_MINUTE 	    0x01
#define DS130X_REG_HOUR  	    0x02
#define DS130X_REG_DOW    	    0x03
#define DS130X_REG_DATE   	    0x04
#define DS130X_REG_MONTH  	    0x05
#define DS130X_REG_YEAR   	    0x06
#define DS130X_REG_CONTROL 	    0x07
#define DS130X_REG_RAM_UTC_HR   0x08
#define DS130X_REG_RAM_UTC_MIN	0x09
#define DS130X_REG_RAM_CENT    	0x0A
#define DS130X_REG_RAM_BEGIN	0x0B
#define DS130X_REG_RAM_END      0x3F
#define DS130X_TIMEOUT		    100
/*----------------------------------------------------------------------------*/
extern I2C_HandleTypeDef *_ds130x_ui2c;

typedef enum DS130X_Rate{
	DS130X_1HZ, DS130X_4096HZ, DS130X_8192HZ, DS130X_32768HZ
} DS130X_Rate;

typedef enum DS130X_SquareWaveEnable{
	DS130X_DISABLED, DS130X_ENABLED
} DS130X_SquareWaveEnable;

DS130X_STATUS DS130X_Init();

DS130X_STATUS DS130X_SetClockHalt(uint8_t halt);
DS130X_STATUS DS130X_GetClockHalt(uint8_t* res);


DS130X_STATUS DS130X_SetRegByte(uint8_t regAddr, uint8_t val);
DS130X_STATUS DS130X_GetRegByte(uint8_t regAddr, uint8_t* res);

DS130X_STATUS DS130X_SetEnableSquareWave(DS130X_SquareWaveEnable mode);
DS130X_STATUS DS130X_SetInterruptRate(DS130X_Rate rate);

DS130X_STATUS DS130X_GetDayOfWeek(uint8_t* res);
DS130X_STATUS DS130X_GetDate(uint8_t* res);
DS130X_STATUS DS130X_GetMonth(uint8_t* res);
DS130X_STATUS DS130X_GetYear(uint16_t* res);

DS130X_STATUS DS130X_GetHour(uint8_t* res);
DS130X_STATUS DS130X_GetMinute(uint8_t* res);
DS130X_STATUS DS130X_GetSecond(uint8_t* res);
DS130X_STATUS DS130X_GetTimeZoneHour(int8_t* res);
DS130X_STATUS DS130X_GetTimeZoneMin(int8_t* res);

DS130X_STATUS DS130X_SetDayOfWeek(uint8_t dow);
DS130X_STATUS DS130X_SetDate(uint8_t date);
DS130X_STATUS DS130X_SetMonth(uint8_t month);
DS130X_STATUS DS130X_SetYear(uint16_t year);

DS130X_STATUS DS130X_SetHour(uint8_t hour_24mode);
DS130X_STATUS DS130X_SetMinute(uint8_t minute);
DS130X_STATUS DS130X_SetSecond(uint8_t second);
DS130X_STATUS DS130X_SetTimeZone(int8_t hr, uint8_t min);

uint8_t DS130X_DecodeBCD(uint8_t bin);
uint8_t DS130X_EncodeBCD(uint8_t dec);


#endif


#ifdef __cplusplus
}
#endif

#endif /* INC_DS130X_DRIVER_H_ */
