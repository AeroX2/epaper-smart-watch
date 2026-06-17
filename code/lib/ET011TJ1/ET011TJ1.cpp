#include "ET011TJ1.h"

ET011TJ1::ET011TJ1(int sck_pin, int mosi_pin, int cs_pin, int dc_pin, int rst_pin, int busy_pin) {
    _sck_pin = sck_pin;
    _mosi_pin = mosi_pin;
    _cs_pin = cs_pin;
    _dc_pin = dc_pin;
    _rst_pin = rst_pin;
    _busy_pin = busy_pin;
    
    // Use hardware SPI with 4MHz, MSB first, SPI mode 0
    _spi_settings = SPISettings(4000000, MSBFIRST, SPI_MODE0);
    _spi = &SPI;
}

void ET011TJ1::begin() {
    // Initialize pins
    if (_cs_pin >= 0) {
        pinMode(_cs_pin, OUTPUT);
        digitalWrite(_cs_pin, HIGH);
    }
    
    if (_dc_pin >= 0) {
        pinMode(_dc_pin, OUTPUT);
        digitalWrite(_dc_pin, LOW);
    }
    
    if (_rst_pin >= 0) {
        pinMode(_rst_pin, OUTPUT);
        digitalWrite(_rst_pin, HIGH);
    }
    
    if (_busy_pin >= 0) {
        pinMode(_busy_pin, INPUT);
    }
    
    // Initialize SPI
    _spi->begin();
    
    // Hardware reset
    reset();
    
    // Wait for ready
    waitWhileBusy();
    
    // Basic initialization sequence
    setPanelSettings();
    setPowerSettings();
    setBoosterSoftStart();
    setPLLControl();
    powerOn();
    
    // Wait for power-on
    waitWhileBusy();
    
    Serial.println("ET011TJ1 initialized");
}

void ET011TJ1::end() {
    powerOff();
    sleep();
    _spi->end();
}

void ET011TJ1::reset() {
    if (_rst_pin >= 0) {
        digitalWrite(_rst_pin, LOW);
        delay(200);
        digitalWrite(_rst_pin, HIGH);
        delay(200);
    } else {
        // Software reset if no reset pin
        Serial.println("Warning: No reset pin defined");
    }
}

void ET011TJ1::waitWhileBusy() {
    if (_busy_pin >= 0) {
        unsigned long timeout = millis() + 5000; // 5 second timeout
        while (digitalRead(_busy_pin) == HIGH) {
            delay(10);
            if (millis() > timeout) {
                Serial.println("Warning: Busy timeout");
                break;
            }
        }
    } else {
        // Fixed delay if no busy pin
        delay(500);
    }
}

void ET011TJ1::writeCommand(uint8_t command) {
    if (_cs_pin >= 0) digitalWrite(_cs_pin, LOW);
    if (_dc_pin >= 0) digitalWrite(_dc_pin, LOW);  // Command mode
    
    _spi->beginTransaction(_spi_settings);
    _spi->transfer(command);
    _spi->endTransaction();
    
    if (_cs_pin >= 0) digitalWrite(_cs_pin, HIGH);
}

void ET011TJ1::writeData(uint8_t data) {
    if (_cs_pin >= 0) digitalWrite(_cs_pin, LOW);
    if (_dc_pin >= 0) digitalWrite(_dc_pin, HIGH);  // Data mode
    
    _spi->beginTransaction(_spi_settings);
    _spi->transfer(data);
    _spi->endTransaction();
    
    if (_cs_pin >= 0) digitalWrite(_cs_pin, HIGH);
}

void ET011TJ1::writeDataBuffer(const uint8_t* data, uint32_t length) {
    if (_cs_pin >= 0) digitalWrite(_cs_pin, LOW);
    if (_dc_pin >= 0) digitalWrite(_dc_pin, HIGH);  // Data mode
    
    _spi->beginTransaction(_spi_settings);
    for (uint32_t i = 0; i < length; i++) {
        _spi->transfer(data[i]);
    }
    _spi->endTransaction();
    
    if (_cs_pin >= 0) digitalWrite(_cs_pin, HIGH);
}

uint8_t ET011TJ1::readData() {
    uint8_t data = 0;
    
    if (_cs_pin >= 0) digitalWrite(_cs_pin, LOW);
    if (_dc_pin >= 0) digitalWrite(_dc_pin, HIGH);  // Data mode
    
    _spi->beginTransaction(_spi_settings);
    data = _spi->transfer(0x00);  // Send dummy byte to read
    _spi->endTransaction();
    
    if (_cs_pin >= 0) digitalWrite(_cs_pin, HIGH);
    
    return data;
}

void ET011TJ1::powerOn() {
    writeCommand(ET011TJ1_PON);
}

void ET011TJ1::powerOff() {
    writeCommand(ET011TJ1_POF);
}

void ET011TJ1::sleep() {
    writeCommand(ET011TJ1_SLP);
    writeData(0x01);  // Check code
}

void ET011TJ1::deepSleep() {
    writeCommand(ET011TJ1_DSLP);
    writeData(0x01);  // Check code
}

void ET011TJ1::setPanelSettings(uint8_t resolution, uint8_t lut_selection, uint8_t booster_switch, uint8_t reset_setting, uint8_t reg_enable) {
    writeCommand(ET011TJ1_PSR);
    
    uint8_t reg_data = 0;
    reg_data |= (resolution & 0x03) << 6;      // D7-D6: Resolution
    reg_data |= (lut_selection & 0x01) << 5;   // D5: LUT selection
    reg_data |= (booster_switch & 0x01) << 4;  // D4: Booster switch
    reg_data |= (reset_setting & 0x01) << 1;   // D1: Reset setting
    reg_data |= (reg_enable & 0x01);           // D0: Register enable
    
    writeData(reg_data);
}

void ET011TJ1::setPowerSettings(uint8_t vs_enable, uint8_t vg_enable) {
    writeCommand(ET011TJ1_PWR);
    
    uint8_t reg_data = 0;
    reg_data |= (vs_enable & 0x01) << 1;  // D1: VS_EN
    reg_data |= (vg_enable & 0x01);       // D0: VG_EN
    
    writeData(reg_data);
}

void ET011TJ1::setBoosterSoftStart(uint8_t phase_a_strength, uint8_t phase_a_time, uint8_t phase_b_strength, uint8_t phase_b_time, uint8_t phase_c_strength, uint8_t phase_c_time) {
    writeCommand(ET011TJ1_BTST);
    
    // Phase A
    uint8_t btpha = ((phase_a_time & 0x03) << 6) | ((phase_a_strength & 0x07) << 3);
    writeData(btpha);
    
    // Phase B  
    uint8_t btphb = ((phase_b_time & 0x03) << 6) | ((phase_b_strength & 0x07) << 3);
    writeData(btphb);
    
    // Phase C
    uint8_t btphc = ((phase_c_time & 0x03) << 6) | ((phase_c_strength & 0x07) << 3);
    writeData(btphc);
}

void ET011TJ1::setPLLControl(uint8_t lclk_setting) {
    writeCommand(ET011TJ1_LPRD);
    writeData(lclk_setting & 0x7F);
}

void ET011TJ1::setWindow(uint16_t x_start, uint16_t y_start, uint16_t width, uint16_t height) {
    writeCommand(ET011TJ1_DTMW);
    
    // X start position
    writeData((x_start >> 8) & 0xFF);
    writeData(x_start & 0xFF);
    
    // Y start position  
    writeData((y_start >> 8) & 0xFF);
    writeData(y_start & 0xFF);
    
    // Width
    writeData((width >> 8) & 0xFF);
    writeData(width & 0xFF);
    
    // Height
    writeData((height >> 8) & 0xFF);
    writeData(height & 0xFF);
}

void ET011TJ1::writeImageDataOld(const uint8_t* image_data, uint32_t length) {
    writeCommand(ET011TJ1_DTM1);
    writeDataBuffer(image_data, length);
}

void ET011TJ1::writeImageDataNew(const uint8_t* image_data, uint32_t length) {
    writeCommand(ET011TJ1_DTM2);
    writeDataBuffer(image_data, length);
}

void ET011TJ1::writeImageDataKW(const uint8_t* image_data, uint32_t length) {
    writeCommand(ET011TJ1_DTM3);
    writeDataBuffer(image_data, length);
}

void ET011TJ1::refreshDisplay(uint8_t pscan_enable, uint8_t regal_enable, ET011TJ1_Mode mode, uint16_t x_start, uint16_t y_start, uint16_t width, uint16_t height) {
    writeCommand(ET011TJ1_DRF);
    
    // Control byte
    uint8_t control = 0;
    control |= (pscan_enable & 0x01) << 7;  // PSCAN
    control |= (regal_enable & 0x01) << 6;  // REGAL_EN
    control |= (mode & 0x03);               // MODE
    writeData(control);
    
    // X start position
    writeData((x_start >> 8) & 0xFF);
    writeData(x_start & 0xFF);
    
    // Y start position
    writeData((y_start >> 8) & 0xFF);
    writeData(y_start & 0xFF);
    
    // Width
    writeData((width >> 8) & 0xFF);
    writeData(width & 0xFF);
    
    // Height  
    writeData((height >> 8) & 0xFF);
    writeData(height & 0xFF);
}

int16_t ET011TJ1::readTemperature() {
    writeCommand(ET011TJ1_TSC);
    
    uint8_t temp_high = readData();
    uint8_t temp_low = readData();
    
    // Convert to temperature value based on lookup table
    uint16_t temp_raw = (temp_high << 8) | temp_low;
    
    // Temperature conversion (simplified - refer to datasheet table for exact mapping)
    if (temp_raw >= 0x0000 && temp_raw <= 0x0FFF) {
        return (int16_t)((temp_raw * 100) / 0x0FFF);  // Scale to 0-100°C range
    }
    
    return -999;  // Error value
}

void ET011TJ1::enableTemperatureSensor(bool enable, uint8_t offset) {
    writeCommand(ET011TJ1_TSE);
    
    uint8_t tse_byte = enable ? 0x00 : 0x01;  // 0: Enable, 1: Disable
    writeData(tse_byte);
    writeData(offset & 0x0F);
}

void ET011TJ1::writeTemperatureSensor(uint8_t wattr, uint8_t wmsb, uint8_t wlsb) {
    writeCommand(ET011TJ1_TSW);
    writeData(wattr);
    writeData(wmsb);
    writeData(wlsb);
}

void ET011TJ1::clearDisplay(ET011TJ1_Color color) {
    // Create buffer filled with specified color
    uint32_t buffer_size = (ET011TJ1_WIDTH * ET011TJ1_HEIGHT) / 4;  // 2 bits per pixel, 4 pixels per byte
    uint8_t* buffer = (uint8_t*)malloc(buffer_size);
    
    if (buffer) {
        // Pack 4 pixels per byte
        uint8_t packed_color = (color << 6) | (color << 4) | (color << 2) | color;
        memset(buffer, packed_color, buffer_size);
        
        writeImageDataNew(buffer, buffer_size);
        refreshDisplay();
        waitWhileBusy();
        
        free(buffer);
        Serial.println("Display cleared");
    } else {
        Serial.println("Error: Could not allocate memory for clear buffer");
    }
}

void ET011TJ1::displayFullImage(const uint8_t* image_data) {
    // Convert and write image data to new SRAM
    uint32_t buffer_size = (ET011TJ1_WIDTH * ET011TJ1_HEIGHT) / 4;
    uint8_t* converted_buffer = (uint8_t*)malloc(buffer_size);
    
    if (converted_buffer) {
        convertImage2Bit(image_data, converted_buffer, ET011TJ1_WIDTH * ET011TJ1_HEIGHT);
        
        writeImageDataNew(converted_buffer, buffer_size);
        refreshDisplay();
        waitWhileBusy();
        
        free(converted_buffer);
        Serial.println("Full image displayed");
    } else {
        Serial.println("Error: Could not allocate memory for image buffer");
    }
}

void ET011TJ1::displayPartialImage(const uint8_t* image_data, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    // Set window for partial update
    setWindow(x, y, width, height);
    
    // Convert and write image data
    uint32_t pixel_count = width * height;
    uint32_t buffer_size = (pixel_count + 3) / 4;  // Round up for partial pixels
    uint8_t* converted_buffer = (uint8_t*)malloc(buffer_size);
    
    if (converted_buffer) {
        convertImage2Bit(image_data, converted_buffer, pixel_count);
        
        writeImageDataNew(converted_buffer, buffer_size);
        refreshDisplay(0, 0, ET011TJ1_MODE_KWG, x, y, width, height);
        waitWhileBusy();
        
        free(converted_buffer);
        Serial.println("Partial image displayed");
    } else {
        Serial.println("Error: Could not allocate memory for partial image buffer");
    }
}

void ET011TJ1::convertImage2Bit(const uint8_t* src_image, uint8_t* dst_buffer, uint32_t pixel_count) {
    for (uint32_t i = 0; i < pixel_count; i += 4) {
        uint8_t packed_byte = 0;
        
        // Pack 4 pixels (2 bits each) into one byte
        for (int j = 0; j < 4 && (i + j) < pixel_count; j++) {
            uint8_t pixel = src_image[i + j] & 0x03;  // Mask to 2 bits
            packed_byte |= (pixel << (6 - j * 2));
        }
        
        dst_buffer[i / 4] = packed_byte;
    }
}

void ET011TJ1::createTestPattern(uint8_t* image_buffer, uint8_t pattern_type) {
    for (int y = 0; y < ET011TJ1_HEIGHT; y++) {
        for (int x = 0; x < ET011TJ1_WIDTH; x++) {
            int pixel_index = y * ET011TJ1_WIDTH + x;
            
            switch (pattern_type) {
                case 0: // Gradient pattern
                    if (x < ET011TJ1_WIDTH / 4) {
                        image_buffer[pixel_index] = ET011TJ1_BLACK;
                    } else if (x < ET011TJ1_WIDTH / 2) {
                        image_buffer[pixel_index] = ET011TJ1_GRAY1;
                    } else if (x < 3 * ET011TJ1_WIDTH / 4) {
                        image_buffer[pixel_index] = ET011TJ1_GRAY2;
                    } else {
                        image_buffer[pixel_index] = ET011TJ1_WHITE;
                    }
                    break;
                    
                case 1: // Checkerboard pattern
                    {
                        int check_x = x / 20;
                        int check_y = y / 20;
                        if ((check_x + check_y) % 2 == 0) {
                            image_buffer[pixel_index] = ET011TJ1_BLACK;
                        } else {
                            image_buffer[pixel_index] = ET011TJ1_WHITE;
                        }
                    }
                    break;
                    
                case 2: // Border test
                    if (x < 10 || x >= ET011TJ1_WIDTH - 10 || y < 10 || y >= ET011TJ1_HEIGHT - 10) {
                        image_buffer[pixel_index] = ET011TJ1_BLACK;
                    } else {
                        image_buffer[pixel_index] = ET011TJ1_WHITE;
                    }
                    break;
                    
                default: // Solid color
                    image_buffer[pixel_index] = ET011TJ1_WHITE;
                    break;
            }
        }
    }
}