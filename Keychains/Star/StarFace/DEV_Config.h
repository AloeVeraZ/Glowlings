/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author      :
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2021-03-16
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of theex Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include "stdio.h"

#include <Arduino.h>
#include "driver/spi_master.h"
#include <Wire.h>
/**
 * data
 **/
#define UBYTE uint8_t
#define UWORD uint16_t
#define UDOUBLE uint32_t


// LCD frames go out as DMA chunks of 64 full rows (the S3 limit is 32 KB);
// four chunks cover a whole 240x240 frame of 16-bit pixels.
#define LCD_SPI_CHUNK_BYTES (64 * 240 * 2)
#define LCD_SPI_QUEUE 4
#define LCD_SPI_MAX_BYTES LCD_SPI_CHUNK_BYTES

/**
 * GPIOI config
 **/

#define LCD_DC_PIN      (8)
#define LCD_CS_PIN      (9)
#define LCD_CLK_PIN     (10)
#define LCD_MOSI_PIN    (11)
#define LCD_MISO_PIN    (12)
#define LCD_RST_PIN     (14)
#define LCD_BL_PIN      (2)

#define DEV_SDA_PIN     (6)
#define DEV_SCL_PIN     (7)

#define Touch_INT_PIN   (5)
#define Touch_RST_PIN   (13)

#define BAT_ADC_PIN     (1)

/*------------------------------------------------------------------------------------------------------*/

void DEV_Digital_Write(uint16_t Pin, uint8_t Value);

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode);
void DEV_SPI_WriteByte(uint8_t Value);
void DEV_SPI_Write_nByte(uint8_t *pData, uint32_t Len);

void DEV_Delay_ms(uint32_t xms);

void DEV_I2C_Write_Byte(uint8_t addr, uint8_t reg, uint8_t Value);

void DEV_I2C_Read_nByte(uint8_t addr, uint8_t reg, uint8_t *pData, uint32_t Len);

void DEV_SET_PWM(uint8_t Value);

uint8_t DEV_Module_Init(void);

#endif
