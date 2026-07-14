#pragma once

// LCDWIKI ES3N28P: 2.8-inch, 240x320 ILI9341V IPS display.
#define BOARD_LCD_HOST SPI2_HOST
#define BOARD_LCD_WIDTH 240
#define BOARD_LCD_HEIGHT 320
#define BOARD_LCD_CS_GPIO 10
#define BOARD_LCD_DC_GPIO 46
#define BOARD_LCD_SCLK_GPIO 12
#define BOARD_LCD_MOSI_GPIO 11
#define BOARD_LCD_MISO_GPIO 13
#define BOARD_LCD_BACKLIGHT_GPIO 45

// The board's microSD socket is wired to SDMMC slot 1 in four-bit mode.
#define BOARD_SD_CLK_GPIO 38
#define BOARD_SD_CMD_GPIO 40
#define BOARD_SD_D0_GPIO 39
#define BOARD_SD_D1_GPIO 41
#define BOARD_SD_D2_GPIO 48
#define BOARD_SD_D3_GPIO 47

// ES8311 codec and FM8002E amplifier path. Audio enable is active low.
#define BOARD_AUDIO_ENABLE_GPIO 1
#define BOARD_I2S_MCLK_GPIO 4
#define BOARD_I2S_BCLK_GPIO 5
// GPIO 8 is ES8311 DSDIN (audio from the ESP32 to the codec). GPIO 6 is
// DSDOUT, the opposite direction used for the on-board microphone.
#define BOARD_I2S_DOUT_GPIO 8
#define BOARD_I2S_LRCK_GPIO 7
#define BOARD_I2S_DIN_GPIO 8

// The audio codec shares the board I2C bus. Its common address is 0x18.
#define BOARD_I2C_SDA_GPIO 16
#define BOARD_I2C_SCL_GPIO 15
#define BOARD_ES8311_I2C_ADDRESS 0x18

// The battery is connected to GPIO 9 through the board's 200K/200K divider.
#define BOARD_BATTERY_ADC_UNIT ADC_UNIT_1
#define BOARD_BATTERY_ADC_CHANNEL ADC_CHANNEL_8
#define BOARD_BATTERY_VOLTAGE_MULTIPLIER 2
