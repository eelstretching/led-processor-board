/*
 * Board definition for the LED processor board: the RP2350B + RM2 reference core
 * (Raspberry Pi RP2350B minimal design, RM2 radio, 8 MB PSRAM) plus a DS3231 RTC.
 *
 * Based on the pico-sdk's boards/pico2_w.h and boards/sparkfun_iotredboard_rp2350.h,
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------
// Use with:  set(PICO_BOARD led_processor_board)
//            set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
// (both before pico_sdk_init / project()).

#ifndef _BOARDS_LED_PROCESSOR_BOARD_H
#define _BOARDS_LED_PROCESSOR_BOARD_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

// For board detection
#define LED_PROCESSOR_BOARD

// --- RP2350 VARIANT ---
#define PICO_RP2350A 0 // 1 for RP2350A, 0 for RP2350B

// --- BOARD SPECIFIC ---
// GPIO 0-31 are the 32 LED outputs (J5-J8), GPIO 32-34 the DS3231, GPIO 35-42 the J2 spare
// header. GPIO 43-46 drive the RM2, GPIO 47 is the PSRAM chip select.
#define LED_PROCESSOR_BOARD_PSRAM_CS_PIN 47

// DS3231 RTC (U6) on I2C0, with 10k pull-ups on the board.
#define LED_PROCESSOR_BOARD_RTC_I2C 0
#define LED_PROCESSOR_BOARD_RTC_SDA_PIN 32
#define LED_PROCESSOR_BOARD_RTC_SCL_PIN 33
#define LED_PROCESSOR_BOARD_RTC_INT_PIN 34

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 1
#endif
// UART1 on the J2 spare header (GPIO 0 and 1 are LED outputs here).
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 36
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 37
#endif

// --- LED ---
// No GPIO LED: the status LED (D1) is on the radio's WL_GPIO0, see CYW43_WL_GPIO_LED_PIN.

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C LED_PROCESSOR_BOARD_RTC_I2C
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN LED_PROCESSOR_BOARD_RTC_SDA_PIN
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN LED_PROCESSOR_BOARD_RTC_SCL_PIN
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- PSRAM ---
#ifndef PICO_PSRAM_CS_PIN
#define PICO_PSRAM_CS_PIN LED_PROCESSOR_BOARD_PSRAM_CS_PIN
#endif

pico_board_cmake_set_default(PICO_PSRAM_SIZE_BYTES, (8 * 1024 * 1024))
#ifndef PICO_PSRAM_SIZE_BYTES
#define PICO_PSRAM_SIZE_BYTES (8 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- CYW43 (RM2) ---

#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif

#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif

// cyw43 SPI pins can't be changed at runtime
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif

// gpio pin to power up the cyw43 chip (WL_ON and BT_ON tied together)
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 43u
#endif

// gpio pin for spi data out to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 45u
#endif

// gpio pin for spi data in from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 45u
#endif

// gpio (irq) pin for the irq line from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 45u
#endif

// gpio pin for the spi clock line to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 46u
#endif

// gpio pin for the spi chip select to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 44u
#endif

#endif // _BOARDS_LED_PROCESSOR_BOARD_H
