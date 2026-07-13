#pragma once

#include <stdbool.h>

#include "settings.h"

typedef void (*web_play_callback_t)(void);

void web_server_start(adhan_settings_t *settings, bool *sd_mounted, bool *audio_available,
                      web_play_callback_t play_callback);
