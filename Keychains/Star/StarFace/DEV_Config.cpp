/*****************************************************************************
* | File      	:   DEV_Config.c
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
#include "DEV_Config.h"
#include "FaceLog.h"

// The panel is write-only and its chip select stays low, so the IDF SPI
// master drives it directly. Frames go out by DMA: the CPU sleeps while the
// pixels stream instead of feeding the SPI FIFO 64 bytes at a time.
static spi_device_handle_t lcdSpi = nullptr;
void DEV_Digital_Write(uint16_t Pin, uint8_t Value)
{
    digitalWrite(Pin, Value);
}

void DEV_SPI_WriteByte(uint8_t Value)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = Value;
    spi_device_polling_transmit(lcdSpi, &t);
}

void DEV_SPI_Write_nByte(uint8_t pData[], uint32_t Len)
{
    // One SPI transaction carries at most 32 KB on the ESP32-S3, so queue the
    // band in chunks back to back, then sleep until the last one is out.
    static spi_transaction_t chunks[LCD_SPI_QUEUE];
    int queued = 0;
    esp_err_t err = ESP_OK;
    while (Len && queued < LCD_SPI_QUEUE && err == ESP_OK) {
        uint32_t n = Len < LCD_SPI_CHUNK_BYTES ? Len : LCD_SPI_CHUNK_BYTES;
        spi_transaction_t &t = chunks[queued];
        t = {};
        t.length = n * 8;
        t.tx_buffer = pData;
        err = spi_device_queue_trans(lcdSpi, &t, portMAX_DELAY);
        if (err == ESP_OK) { ++queued; pData += n; Len -= n; }
    }
    for (int i = 0; i < queued; ++i) {
        spi_transaction_t *done;
        spi_device_get_trans_result(lcdSpi, &done, portMAX_DELAY);
    }
    static bool reported = false;
    if ((err != ESP_OK || Len) && !reported) {
        reported = true;
        FACE_LOG(printf, "LCD DMA transfer failed (%d, %lu bytes left)\n", int(err), (unsigned long)Len);
    }
}

void DEV_I2C_Write_Byte(uint8_t addr, uint8_t reg, uint8_t Value)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(Value);
    Wire.endTransmission();
}

void DEV_I2C_Read_nByte(uint8_t addr, uint8_t reg, uint8_t *pData, uint32_t Len)
{
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission();
    
    Wire.requestFrom(addr, Len);
  
    uint8_t i = 0;
    for(i = 0; i < Len; i++) {
      pData[i] =  Wire.read();
    }
    Wire.endTransmission();
}

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode)
{

    if (Mode == 0 )
    {
        pinMode(Pin, INPUT);
    }
    else
    {
        pinMode(Pin, OUTPUT);
    }
}

void DEV_SET_PWM(uint8_t Value)
{
    if (Value > 100)
    {
        FACE_LOG(printf, "DEV_SET_PWM Error \r\n");
    }
    else
    {
        analogWrite(LCD_BL_PIN, Value * 2.55);
    }
}

void DEV_Delay_ms(uint32_t xms)
{
    delay(xms);
}

void DEV_GPIO_Init(void)
{

    DEV_GPIO_Mode(LCD_RST_PIN, 1);
    DEV_GPIO_Mode(LCD_DC_PIN, 1);
    DEV_GPIO_Mode(LCD_CS_PIN, 1);
    DEV_GPIO_Mode(LCD_BL_PIN, 1);
    DEV_GPIO_Mode(Touch_RST_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 1);
    DEV_Digital_Write(LCD_DC_PIN, 0);
    // PWM Config
    DEV_Digital_Write(LCD_BL_PIN, 0);
}

uint8_t DEV_Module_Init(void)
{
    DEV_Delay_ms(100);
    // GPIO Config
    DEV_GPIO_Init();
    // SPI Config
    if (!lcdSpi) {  // the bus survives a re-init within one boot
        spi_bus_config_t bus = {};
        bus.mosi_io_num = LCD_MOSI_PIN;
        bus.miso_io_num = -1;
        bus.sclk_io_num = LCD_CLK_PIN;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = LCD_SPI_MAX_BYTES;
        spi_device_interface_config_t dev = {};
        dev.clock_speed_hz = 80000000;
        dev.mode = 0;
        dev.spics_io_num = -1;  // CS is held low by the panel driver
        dev.queue_size = LCD_SPI_QUEUE;
        dev.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
        if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK ||
            spi_bus_add_device(SPI2_HOST, &dev, &lcdSpi) != ESP_OK)
            FACE_LOG(println, "LCD SPI setup failed");
    }
    
    // I2C Config
    Wire.setPins(DEV_SDA_PIN, DEV_SCL_PIN);
    Wire.setClock(400000);
    Wire.begin();
    FACE_LOG(printf, "DEV_Module_Init OK \r\n");
    return 0;
}
