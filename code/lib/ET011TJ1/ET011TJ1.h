#ifndef ET011TJ1_H
#define ET011TJ1_H

#include <Arduino.h>
#include <SPI.h>

// ET011TJ1 240x240 E-paper Display Driver for Arduino
// Based on technical specification ET011TJ1 Version 1.0

#define ET011TJ1_WIDTH  240
#define ET011TJ1_HEIGHT 240

// Command definitions
#define ET011TJ1_PSR      0x01  // Panel Setting Register
#define ET011TJ1_PWR      0x02  // Power Setting Register  
#define ET011TJ1_POF      0x03  // Power OFF Command
#define ET011TJ1_PON      0x04  // Power ON Command
#define ET011TJ1_BTST     0x06  // Booster Soft Start
#define ET011TJ1_DSLP     0x07  // Deep Sleep
#define ET011TJ1_SLP      0x08  // Sleep
#define ET011TJ1_DTM1     0x10  // Data Start Transmission 1 (old SRAM)
#define ET011TJ1_DTM2     0x11  // Data Start Transmission 2 (new SRAM)
#define ET011TJ1_DRF      0x12  // Display Refresh
#define ET011TJ1_DTM3     0x13  // Data Start Transmission 3 (KW mode)
#define ET011TJ1_DTMW     0x14  // Data Start Transmission Window Register
#define ET011TJ1_DTM4     0x16  // Data Start Transmission 4 (KW mode)
#define ET011TJ1_LPRD     0x30  // PLL Control
#define ET011TJ1_TSC      0x40  // Temperature Sensor Command
#define ET011TJ1_TSE      0x41  // Temperature Sensor Enable
#define ET011TJ1_TSW      0x42  // Temperature Sensor Write

// Color definitions for 2-bit grayscale
enum ET011TJ1_Color {
    ET011TJ1_BLACK = 0x00,
    ET011TJ1_GRAY1 = 0x01,
    ET011TJ1_GRAY2 = 0x02,
    ET011TJ1_WHITE = 0x03
};

// Display modes
enum ET011TJ1_Mode {
    ET011TJ1_MODE_KWG = 0x00,  // KWG (K/W/Gray) mode
    ET011TJ1_MODE_KW  = 0x01,  // KW (K/W) mode
    ET011TJ1_MODE_KWR = 0x02   // KWR (K/W/Red) mode
};

class ET011TJ1 {
private:
    int _sck_pin;
    int _mosi_pin;
    int _cs_pin;
    int _dc_pin;
    int _rst_pin;
    int _busy_pin;
    
    SPIClass* _spi;
    SPISettings _spi_settings;
    
    void writeCommand(uint8_t command);
    void writeData(uint8_t data);
    void writeDataBuffer(const uint8_t* data, uint32_t length);
    uint8_t readData();
    void waitWhileBusy();
    void reset();

public:
    // Constructor with pin assignments based on wiring diagram
    ET011TJ1(int sck_pin = PA1, int mosi_pin = -1, int cs_pin = PB8, 
             int dc_pin = PA6, int rst_pin = -1, int busy_pin = PA2);
    
    // Initialize display
    void begin();
    void end();
    
    // Power control
    void powerOn();
    void powerOff();
    void sleep();
    void deepSleep();
    
    // Configuration
    void setPanelSettings(uint8_t resolution = 0, uint8_t lut_selection = 0, 
                         uint8_t booster_switch = 0, uint8_t reset_setting = 0, 
                         uint8_t reg_enable = 0);
    void setPowerSettings(uint8_t vs_enable = 0, uint8_t vg_enable = 0);
    void setBoosterSoftStart(uint8_t phase_a_strength = 1, uint8_t phase_a_time = 1,
                            uint8_t phase_b_strength = 1, uint8_t phase_b_time = 1,
                            uint8_t phase_c_strength = 1, uint8_t phase_c_time = 1);
    void setPLLControl(uint8_t lclk_setting = 0x12);  // Default 19 clock cycles
    
    // Window operations
    void setWindow(uint16_t x_start, uint16_t y_start, uint16_t width, uint16_t height);
    
    // Image data transmission
    void writeImageDataOld(const uint8_t* image_data, uint32_t length);
    void writeImageDataNew(const uint8_t* image_data, uint32_t length);
    void writeImageDataKW(const uint8_t* image_data, uint32_t length);
    
    // Display refresh
    void refreshDisplay(uint8_t pscan_enable = 0, uint8_t regal_enable = 0, 
                       ET011TJ1_Mode mode = ET011TJ1_MODE_KWG,
                       uint16_t x_start = 0, uint16_t y_start = 0, 
                       uint16_t width = ET011TJ1_WIDTH, uint16_t height = ET011TJ1_HEIGHT);
    
    // Temperature sensor
    int16_t readTemperature();
    void enableTemperatureSensor(bool enable = true, uint8_t offset = 0);
    void writeTemperatureSensor(uint8_t wattr, uint8_t wmsb, uint8_t wlsb);
    
    // High-level display functions
    void clearDisplay(ET011TJ1_Color color = ET011TJ1_WHITE);
    void displayFullImage(const uint8_t* image_data);
    void displayPartialImage(const uint8_t* image_data, uint16_t x, uint16_t y, 
                            uint16_t width, uint16_t height);
    
    // Utility functions
    static void convertImage2Bit(const uint8_t* src_image, uint8_t* dst_buffer, uint32_t pixel_count);
    static void createTestPattern(uint8_t* image_buffer, uint8_t pattern_type = 0);
};

#endif // ET011TJ1_H