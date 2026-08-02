#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"

typedef void (*adhan_voice_command_callback_t)(void *context);

typedef struct {
    i2s_chan_handle_t rx_channel;
    adhan_voice_command_callback_t play_adhan;
    adhan_voice_command_callback_t show_next_prayer;
    void *context;
} adhan_voice_commands_config_t;

esp_err_t adhan_voice_commands_start(const adhan_voice_commands_config_t *config);
esp_err_t adhan_voice_commands_pause(void);
esp_err_t adhan_voice_commands_resume(void);
