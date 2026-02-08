/*
 * @file gconfig.example.h
 * @brief Example configuration header for gsystem; copy this file and
 *        enable, disable or customize defines for your target project.
 *
 * Contains commented macros that control optional features, platform
 * selection, peripheral assignments, and debug settings. Projects should
 * create a `gconfig.h` based on this example and enable only the needed
 * options.
 */

#ifndef _G_SYSTEM_CONFIG_EXAMPLE_H_
#define _G_SYSTEM_CONFIG_EXAMPLE_H_


#include "soul.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 * Basic project configuration
 *
 * - `GSYSTEM_RESET_TIMEOUT_MS` : milliseconds before forced reset in system error state.
 * - `GSYSTEM_POCESSES_COUNT`   : predefined number of scheduler processes.
 */
// #define GSYSTEM_RESET_TIMEOUT_MS    (30000)
// #define GSYSTEM_POCESSES_COUNT      (32)

/*
 * Feature toggles (define to "in library" disable feature)
 *
 * Prefix: GSYSTEM_NO_*
 * 
 * - `GSYSTEM_NO_RESTART_W`       : disable system restart cause check.
 * - `GSYSTEM_NO_RTC_W`           : disable RTC support.
 * - `GSYSTEM_NO_RTC_CALENDAR_W`  : disable RTC calendar support.
 * - `GSYSTEM_NO_RTC_INTERNAL_W`  : disable internal RTC backup registers handling.
 * - `GSYSTEM_NO_SYS_TICK_W`      : disable SysTick watchdog and reanimation.
 * - `GSYSTEM_NO_RAM_W`           : disable RAM overflow watchdog.
 * - `GSYSTEM_NO_ADC_W`           : disable automatic ADC voltage measurements.
 * - `GSYSTEM_NO_I2C_W`           : disable I2C bus falling watchdog.
 * - `GSYSTEM_NO_POWER_W`         : disable MCU reference power voltage watchdog.
 * - `GSYSTEM_NO_MEMORY_W`        : disable automatic memory management (enable/reload/reinitilize).
 * - `GSYSTEM_NO_PLL_CHECK_W`     : disable PLL configuration verification at startup.
 * - `GSYSTEM_NO_STORAGE_AT`      : disable StorageAT library driver and storage-based features.
 * - `GSYSTEM_NO_REVISION`        : disable build revision embedding in build.
 * - `GSYSTEM_NO_DEVICE_SETTINGS` : disable user device settings management.
 * - `GSYSTEM_NO_VTOR_REWRITE`    : disable VTOR rewrite at startup (for MCUs with remappable vector table).
 * - `GSYSTEM_NO_TAMPER_RESET`    : disable tamper pin reset handling (if RTC tamper pin is used - STM32F1).
 * 
 * Examples: GSYSTEM_NO_ADC_W disables ADC support, GSYSTEM_NO_RTC_W disables RTC.
 */
// #define GSYSTEM_NO_RESTART_W
// #define GSYSTEM_NO_RTC_W
// #define GSYSTEM_NO_RTC_CALENDAR_W
// #define GSYSTEM_NO_RTC_INTERNAL_W
// #define GSYSTEM_NO_SYS_TICK_W
// #define GSYSTEM_NO_RAM_W
// #define GSYSTEM_NO_ADC_W
// #define GSYSTEM_NO_I2C_W
// #define GSYSTEM_NO_POWER_W
// #define GSYSTEM_NO_MEMORY_W
// #define GSYSTEM_NO_PLL_CHECK_W
// #define GSYSTEM_NO_STORAGE_AT
// #define GSYSTEM_NO_REVISION
// #define GSYSTEM_NO_DEVICE_SETTINGS
// #define GSYSTEM_NO_VTOR_REWRITE
// #define GSYSTEM_NO_TAMPER_RESET

/*
 * Debug and print controls
 *
 * Define `GSYSTEM_NO_STATUS_PRINT`, `GSYSTEM_NO_PRINTF` etc to reduce binary size
 * by disabling status and print helpers.
 * 
 * - `GSYSTEM_NO_STATUS_PRINT` : disable soul.h status-to-string conversion and show functions.
 * - `GSYSTEM_NO_PRINTF`       : disable all printf-style debug output.
 * - `GSYSTEM_NO_CPU_INFO`     : disable CPU information gathering and printout.
 * - `GSYSTEM_NO_PROC_INFO`    : disable process/scheduler information gathering and printout.
 * - `GSYSTEM_NO_BEDUG`        : disable all debug gsystem output.
 */
// #define GSYSTEM_NO_STATUS_PRINT
// #define GSYSTEM_NO_PRINTF
// #define GSYSTEM_NO_CPU_INFO
// #define GSYSTEM_NO_PROC_INFO
// #define GSYSTEM_NO_BEDUG

/*
 * ADC configuration
 *
 * `GSYSTEM_ADC_VOLTAGE_COUNT` - number of ADC channels used for voltage measurements.
 *                               You need to initialize ADC channels rank and DMA in STM32CubeMX 
 *                               or manually before use.
 */
// #define GSYSTEM_ADC_VOLTAGE_COUNT  (1)

/*
 * Storage backend selection
 *
 * - `GSYSTEM_FLASH_MODE` : Use SPI flash (W25Qxx) backend.
 * - `GSYSTEM_EEPROM_MODE`: Use I2C EEPROM (AT24CM01) backend.
 * 
 * Only one mode should typically be enabled.
 */
// #define GSYSTEM_FLASH_MODE
// #define GSYSTEM_EEPROM_MODE

/*
 * External RTC configuration
 *
 * Define the driver to use (e.g. GSYSTEM_DS1302_CLOCK or GSYSTEM_DS1307_CLOCK)
 * and provide pin/I2C handles used by the clock driver.
 * 
 * - `GSYSTEM_DS1302_CLOCK`   : Use DS1302 RTC chip via GPIO bit-banging.
 * - `GSYSTEM_CLOCK_CLK`      : DS1302 clock pin (GPIO_Port, GPIO_Pin).
 * - `GSYSTEM_CLOCK_IO`       : DS1302 data I/O pin (GPIO_Port, GPIO_Pin).
 * - `GSYSTEM_CLOCK_CE`       : DS1302 chip-enable pin (GPIO_Port, GPIO_Pin).
 * - `GSYSTEM_DS1307_CLOCK`   : Use DS1307 RTC chip via I2C interface.
 * - `GSYSTEM_CLOCK_I2C`      : I2C handle used by DS1307 (e.g. hi2c1).
 * - `GSYSTEM_CLOCK_I2C_BASE` : I2C peripheral base (e.g. I2C1).
 * 
 * TODO: use hi2c1 structure member instead of defining GSYSTEM_CLOCK_I2C_BASE
 */
// #define GSYSTEM_DS1302_CLOCK
// #define GSYSTEM_CLOCK_CLK          CLOCK_CLK_GPIO_Port,CLOCK_CLK_Pin
// #define GSYSTEM_CLOCK_IO           CLOCK_IO_GPIO_Port,CLOCK_IO_Pin
// #define GSYSTEM_CLOCK_CE           CLOCK_CE_GPIO_Port,CLOCK_CE_Pin
// #define GSYSTEM_DS1307_CLOCK
// #define GSYSTEM_CLOCK_I2C          (hi2c1)
// #define GSYSTEM_CLOCK_I2C_BASE     I2C1

/*
 * Peripheral assignments
 *
 * - `GSYSTEM_TIMER`            : default hardware gsystem timer (e.g. TIM1)
 * - `GSYSTEM_BEDUG_UART`       : UART handle used for debug messages
 * - `GSYSTEM_I2C`              : default I2C handle used by peripherals for i2c watchdog
 * - `GSYSTEM_EEPROM_I2C`       : I2C handle used for EEPROM (if different from GSYSTEM_I2C)
 * - `GSYSTEM_FLASH_SPI`        : SPI handle used for flash chip
 * - `GSYSTEM_FLASH_CS_PORT`    : chip-select GPIO for SPI flash
 * - `GSYSTEM_FLASH_CS_PIN`     : chip-select pin for SPI flash
 * - `GSYSTEM_MEMORY_DMA`       : enable DMA usage for memory transfers (the driver not woking yet)
 * - `GSYSTEM_MEMORY_STREAM_TX` : SPI DMA stream indices for TX
 * - `GSYSTEM_MEMORY_STREAM_RX` : SPI DMA stream indices for RX
 * 
 * TODO: use pair port,pin for GSYSTEM_FLASH_CS definition instead of separate defines
 * TODO: fix memory DMA usage in W25Qxx driver
 */
// #define GSYSTEM_TIMER              (TIM1)

// #define GSYSTEM_BEDUG_UART         (huart2)

// #define GSYSTEM_I2C                (hi2c2)
// #define GSYSTEM_EEPROM_I2C         (hi2c2)

// #define GSYSTEM_FLASH_SPI          (hspi1)
// #define GSYSTEM_FLASH_CS_PORT      (FLASH1_CS_GPIO_Port)
// #define GSYSTEM_FLASH_CS_PIN       (FLASH1_CS_Pin)
// #define GSYSTEM_MEMORY_DMA
// #define GSYSTEM_MEMORY_STREAM_TX   (3)
// #define GSYSTEM_MEMORY_STREAM_RX   (2)

/*
 * Button subsystem configuration
 *
 * - `GSYSTEM_BUTTONS_COUNT`            : number of buttons the system should track
 * - `GSYSTEM_BUTTON_DEBOUNCE_MS`       : debounce time in ms
 * - `GSYSTEM_BUTTON_CLICKS_TIMEOUT_MS` : window for multi-click detection
 * - `GSYSTEM_BUTTON_CLICKS_DELAY_MS`   : click application delay
 */
// #define GSYSTEM_BUTTONS_COUNT      (0)
// #define GSYSTEM_BUTTON_DEBOUNCE_MS       (60)
// #define GSYSTEM_BUTTON_CLICKS_TIMEOUT_MS (100)
// #define GSYSTEM_BUTTON_CLICKS_DELAY_MS   (400)

/*
 * Custom user device status codes
 *
 * Extend soul.h definitions as needed.
 */
// typedef enum _CUSTOM_SOUL_STATUSES {
// 	CUSTOM_STATUS_01 = RESERVED_STATUS_01,
// 	CUSTOM_STATUS_02 = RESERVED_STATUS_02
// } CUSTOM_SOUL_STATUSES;


/*
 * Device types enumeration
 *
 * Define device types for conditional builds and runtime checks.
 */
// enum DEVICE_TYPE {
// 	DT_DEVICE_1 = 0x0001,
// 	DT_DEVICE_2,
// 	DT_DEVICE_3
// };


/*
 * Device identification and versions
 *
 * - `GSYSTEM_DEVICE_TYPE` : Selects device type for conditional builds
 * - `GSYSTEM_STG_VERSION` : Device settings version, if you change your version
 *                           structure update this value to force settings reset.
 *                           You may also add settings update logic in settings_repair().
 * - `GSYSTEM_FW_VERSION`  : Firmware version identifier
 * - `BUILD_VERSION`       : Build version string used in about/info prints.
 */
#define GSYSTEM_DEVICE_TYPE     (DT_DEVICE_1)

#define GSYSTEM_STG_VERSION     ((uint8_t)0x01)
#define GSYSTEM_FW_VERSION      ((uint8_t)0x01)

#define BUILD_VERSION           "v1.0.0"


#ifdef __cplusplus
}
#endif


#endif
