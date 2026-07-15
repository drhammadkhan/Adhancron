#pragma once

typedef enum {
    AUDIO_TRACK_ADHAN = 0,
    AUDIO_TRACK_EID_TAKBEER,
} audio_track_t;

#define ADHAN_STORAGE_BASE_PATH "/storage"
#define ADHAN_AUDIO_PATH ADHAN_STORAGE_BASE_PATH "/adhan.mp3"
#define ADHAN_UPLOAD_PATH ADHAN_STORAGE_BASE_PATH "/adhan.tmp"
#define TAKBEER_AUDIO_PATH ADHAN_STORAGE_BASE_PATH "/takbeer.mp3"
#define TAKBEER_UPLOAD_PATH ADHAN_STORAGE_BASE_PATH "/takbeer.tmp"
