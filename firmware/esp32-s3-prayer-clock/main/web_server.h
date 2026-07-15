#pragma once

#include <stdbool.h>

#include "audio_storage.h"
#include "settings.h"

typedef void (*web_play_callback_t)(audio_track_t track);

void web_server_start(
    adhan_settings_t *settings,
    bool *storage_mounted,
    bool *adhan_audio_available,
    bool *takbeer_audio_available,
    web_play_callback_t play_callback);
