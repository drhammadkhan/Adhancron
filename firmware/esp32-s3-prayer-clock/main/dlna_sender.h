#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DLNA_DEVICE_ID_SIZE 96
#define DLNA_DEVICE_NAME_SIZE 64
#define DLNA_DEVICE_MODEL_SIZE 64
#define DLNA_DEVICE_URL_SIZE 256
#define DLNA_MAX_DEVICES 12

typedef struct {
    char id[DLNA_DEVICE_ID_SIZE];
    char name[DLNA_DEVICE_NAME_SIZE];
    char model[DLNA_DEVICE_MODEL_SIZE];
    char location[DLNA_DEVICE_URL_SIZE];
    char av_transport_url[DLNA_DEVICE_URL_SIZE];
    char rendering_control_url[DLNA_DEVICE_URL_SIZE];
} dlna_device_t;

size_t dlna_sender_discover(
    dlna_device_t *devices, size_t capacity, uint32_t timeout_ms);
bool dlna_sender_play(
    const char *location,
    const char *media_url,
    int volume,
    char *error,
    size_t error_size);
