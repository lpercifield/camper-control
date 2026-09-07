#pragma once
// -----------------------------------------------------------------------------
// SenseCAP Indicator D1 - ESP32-S3 board support
//
// Pin map confirmed against Seeed's Arduino tutorial, the ESPHome device
// definition and the Arduino_GFX discussion thread for this panel.
// -----------------------------------------------------------------------------

// ---- Display: ST7701S, 480x480, 16-bit RGB parallel -------------------------
#define LCD_H_RES 480
#define LCD_V_RES 480

#define LCD_PIN_DE    18
#define LCD_PIN_VSYNC 17
#define LCD_PIN_HSYNC 16
#define LCD_PIN_PCLK  21

// Red 5 bits, green 6 bits, blue 5 bits (MSB first as Arduino_GFX expects)
#define LCD_PIN_R0 4
#define LCD_PIN_R1 3
#define LCD_PIN_R2 2
#define LCD_PIN_R3 1
#define LCD_PIN_R4 0
#define LCD_PIN_G0 10
#define LCD_PIN_G1 9
#define LCD_PIN_G2 8
#define LCD_PIN_G3 7
#define LCD_PIN_G4 6
#define LCD_PIN_G5 5
#define LCD_PIN_B0 15
#define LCD_PIN_B1 14
#define LCD_PIN_B2 13
#define LCD_PIN_B3 12
#define LCD_PIN_B4 11

// Panel timing (hsync: polarity, front porch, pulse width, back porch)
#define LCD_HSYNC_POLARITY 1
#define LCD_HSYNC_FRONT_PORCH 10
#define LCD_HSYNC_PULSE_WIDTH 8
#define LCD_HSYNC_BACK_PORCH 50
#define LCD_VSYNC_POLARITY 1
#define LCD_VSYNC_FRONT_PORCH 10
#define LCD_VSYNC_PULSE_WIDTH 8
#define LCD_VSYNC_BACK_PORCH 20

// ---- Panel command channel: bit-banged SPI, CS lives on the IO expander -----
#define LCD_SPI_SCK  41
#define LCD_SPI_MOSI 48
#define LCD_SPI_MISO 47

// ---- Backlight (LEDC PWM) ---------------------------------------------------
#define LCD_PIN_BL      45
#define LCD_BL_FREQ_HZ  2000
#define LCD_BL_RES_BITS 8

// ---- I2C bus: FT6336 touch + PCA9535 IO expander ---------------------------
#define I2C_PIN_SDA 39
#define I2C_PIN_SCL 40
#define I2C_FREQ_HZ 400000

#define PCA9535_I2C_ADDR 0x20
// 0x48, not the 0x38 the FT6336 datasheet and every community pin map give.
// Confirmed 2026-09-06 by bus scan and by decoding live touches: the register
// layout is the usual FT5x06/FT6x36 one (0x02 point count, 0x03..0x06 the first
// point) and coordinates land inside 0..479. See docs/HARDWARE.md.
#define TOUCH_I2C_ADDR   0x48  // FT5x06/FT6x36-compatible, non-standard address

// PCA9535 port pins (port 0 = bits 0-7, port 1 = bits 8-15)
#define EXP_LORA_CS    0
#define EXP_LORA_RST   1
#define EXP_LORA_BUSY  2  // input
#define EXP_LORA_DIO1  3  // input
#define EXP_LCD_CS     4
#define EXP_LCD_RST    5
#define EXP_TOUCH_INT  6  // input - leave configured as input
#define EXP_TOUCH_RST  7
#define EXP_SD_CS      8
#define EXP_BUZZER_EN  9  // UNVERIFIED - not driven by this firmware
#define EXP_GROVE_PWR  10 // UNVERIFIED - not driven by this firmware

// ---- Front panel button (active low) ---------------------------------------
#define PIN_USER_BUTTON 38

// ---- UART link to the RP2040 co-processor -----------------------------------
#define RP2040_UART_TX 19
#define RP2040_UART_RX 20
#define RP2040_UART_BAUD 115200
