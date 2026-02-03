// tft_setup.h – ST7789 240x240, fara CS (stil MakerGuides)

#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  240

// Daca vezi culori inversate, comenteaza linia:
#define TFT_RGB_ORDER TFT_BGR

// Pini ESP32
#define TFT_CS    -1
#define TFT_RST   16
#define TFT_DC    17
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_MISO  19

// Backlight
#define TFT_BL    22
#define TFT_BACKLIGHT_ON HIGH

// Fonturi / optiuni
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

// Frecvente SPI
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000
