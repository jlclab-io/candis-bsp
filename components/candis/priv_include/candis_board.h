/* SPDX-License-Identifier: Apache-2.0 */
/* Fixed hardware carried by the Candis CB revision 1.2 core board.
 * Baseboard pins, devices, addresses and models are supplied at runtime. */
#pragma once

#define CANDIS_MAIN_I2C          BSP_I2C_NUM
#define CANDIS_MAIN_SCL          BSP_I2C_SCL
#define CANDIS_MAIN_SDA          BSP_I2C_SDA
#define CANDIS_PMU_ADDR          0x34
#define CANDIS_RTC_ADDR          0x32
#define CANDIS_CODEC_ADDR        0x10
#define CANDIS_TOUCH_ADDR        0x5A

#define CANDIS_LCD_SPI           SPI2_HOST
#define CANDIS_LCD_RESET         GPIO_NUM_2
#define CANDIS_LCD_TE            GPIO_NUM_8
#define CANDIS_LCD_CS            GPIO_NUM_10
#define CANDIS_LCD_CLK           GPIO_NUM_12
#define CANDIS_LCD_D0            GPIO_NUM_11
#define CANDIS_LCD_D1            GPIO_NUM_13
#define CANDIS_LCD_D2            GPIO_NUM_14
#define CANDIS_LCD_D3            GPIO_NUM_9
#define CANDIS_TOUCH_RESET       GPIO_NUM_3
#define CANDIS_TOUCH_INT         GPIO_NUM_1

#define CANDIS_PA_ENABLE         GPIO_NUM_4
#define CANDIS_I2S_MCLK          GPIO_NUM_15
#define CANDIS_I2S_BCLK          GPIO_NUM_16
#define CANDIS_I2S_WS            GPIO_NUM_17
#define CANDIS_I2S_DOUT          GPIO_NUM_18
#define CANDIS_I2S_DIN           GPIO_NUM_19
