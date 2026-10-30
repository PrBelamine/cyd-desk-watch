// TFT_eSPI setup for the ESP32-2432S028R "Cheap Yellow Display" (ILI9341, 240x320).
// Copy this file over  Documents/Arduino/libraries/TFT_eSPI/User_Setup.h  (replace the whole file).
// If the colours look inverted or the image is mirrored (some CYD boards use an ST7789 panel),
// uncomment the ST7789 line and the TFT_INVERSION_ON line instead of ILI9341.
#define USER_SETUP_ID 8001

#define ILI9341_2_DRIVER      // common CYD variant; if the screen stays blank try ILI9341_DRIVER instead
// #define ST7789_DRIVER
// #define TFT_INVERSION_ON

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT

#define USE_HSPI_PORT         // the display uses HSPI; the touch controller uses VSPI (see the sketch)
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
