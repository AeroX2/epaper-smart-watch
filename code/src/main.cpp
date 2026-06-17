#include <Arduino.h>
#include "ET011TJ1.h"

// Define Serial with specific pins PA10 (RX) and PA9 (TX)
HardwareSerial Serial1(PA9, PA10);

// Pin definitions based on wiring diagram
#define EPD_SCK    PA1   // SPI Clock (pin 11 -> PA1)
#define EPD_MOSI   PB5    // Use default SPI MOSI
#define EPD_CS     PB8   // Chip Select (pin 14 -> PB8)
#define EPD_DC     PA6   // Data/Command (pin 12 -> PA6)  
#define EPD_RST    PA7   // Reset (PA7)
#define EPD_BUSY   PA2   // Busy (pin 13 -> PA2)

// Create display instance with pins from wiring diagram
ET011TJ1 display(EPD_SCK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

void setup() {
    Serial1.begin(115200);
    delay(1000);
    
    Serial1.println("ET011TJ1 E-paper Display Test");
    Serial1.println("==============================");
    
    // Initialize display
    Serial1.println("Initializing display...");
    display.begin();
    
    // Clear display to white
    Serial1.println("Clearing display...");
    display.clearDisplay(ET011TJ1_WHITE);
    delay(2000);
    
    // Test pattern 1: Gradient
    Serial1.println("Displaying gradient pattern...");
    uint8_t* image_buffer = (uint8_t*)malloc(ET011TJ1_WIDTH * ET011TJ1_HEIGHT);
    if (image_buffer) {
        ET011TJ1::createTestPattern(image_buffer, 0);  // Gradient pattern
        display.displayFullImage(image_buffer);
        delay(3000);
        
        // Test pattern 2: Checkerboard
        Serial1.println("Displaying checkerboard pattern...");
        ET011TJ1::createTestPattern(image_buffer, 1);  // Checkerboard pattern
        display.displayFullImage(image_buffer);
        delay(3000);
        
        // Test pattern 3: Border
        Serial1.println("Displaying border pattern...");
        ET011TJ1::createTestPattern(image_buffer, 2);  // Border pattern
        display.displayFullImage(image_buffer);
        delay(3000);
        
        free(image_buffer);
    } else {
        Serial1.println("Error: Could not allocate image buffer");
    }
    
    // Test partial update
    Serial1.println("Testing partial update...");
    uint8_t partial_data[50 * 50];
    for (int i = 0; i < 50 * 50; i++) {
        partial_data[i] = ET011TJ1_GRAY1;
    }
    display.displayPartialImage(partial_data, 95, 95, 50, 50);  // Center square
    delay(2000);
    
    // Test temperature sensor
    Serial1.println("Testing temperature sensor...");
    display.enableTemperatureSensor(true);
    int16_t temp = display.readTemperature();
    Serial1.print("Temperature: ");
    Serial1.print(temp);
    Serial1.println("°C");
    
    Serial1.println("Setup complete!");
}

void loop() {
    // Blink test - alternate between black and white every 10 seconds
    static unsigned long last_update = 0;
    static bool is_black = false;
    
    if (millis() - last_update > 10000) {  // 10 second interval
        last_update = millis();
        
        Serial1.println(is_black ? "Clearing to white..." : "Clearing to black...");
        display.clearDisplay(is_black ? ET011TJ1_WHITE : ET011TJ1_BLACK);
        is_black = !is_black;
        
        // Read temperature periodically
        int16_t temp = display.readTemperature();
        Serial1.print("Temperature: ");
        Serial1.print(temp);
        Serial1.println("°C");
    }
    
    delay(100);
}
