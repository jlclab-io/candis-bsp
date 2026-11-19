/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2. See the hardware compatibility and pin map. */
#pragma once

#define CANDIS_MAIN_I2C          I2C_NUM_0
#define CANDIS_MAIN_SCL          BSP_I2C_SCL
#define CANDIS_MAIN_SDA          BSP_I2C_SDA
#define CANDIS_PMU_ADDR          0x34
#define CANDIS_RTC_ADDR          0x32
#define CANDIS_CODEC_ADDR        0x10
#define CANDIS_IMU_ADDR          0x68
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

#define CANDIS_SD_D0             GPIO_NUM_20
#define CANDIS_SD_D1             GPIO_NUM_21
#define CANDIS_SD_D2             GPIO_NUM_22
#define CANDIS_SD_D3             GPIO_NUM_23
#define CANDIS_SD_CLK            GPIO_NUM_24
#define CANDIS_SD_CMD            GPIO_NUM_25
#define CANDIS_FIXTURE_SD_CD     GPIO_NUM_35
#define CANDIS_EXP_SD_CD         GPIO_NUM_39

#define CANDIS_EXP_I2C           I2C_NUM_1
#define CANDIS_EXP_SCL           GPIO_NUM_58
#define CANDIS_EXP_SDA           GPIO_NUM_59
#define CANDIS_EXP_EEPROM_ADDR   0x50
