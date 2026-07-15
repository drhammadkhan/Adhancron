#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAST_DEVICE_ID_SIZE 48
#define CAST_DEVICE_NAME_SIZE 64
#define CAST_DEVICE_MODEL_SIZE 48
#define CAST_DEVICE_HOST_SIZE 16
#define CAST_MAX_DEVICES 16

typedef struct {
    char id[CAST_DEVICE_ID_SIZE];
    char name[CAST_DEVICE_NAME_SIZE];
    char model[CAST_DEVICE_MODEL_SIZE];
    char host[CAST_DEVICE_HOST_SIZE];
    uint16_t port;
    bool is_group;
} cast_device_t;

size_t cast_sender_discover(cast_device_t *devices, size_t capacity, uint32_t timeout_ms);
bool cast_sender_find(const char *device_id, cast_device_t *device, uint32_t timeout_ms);
bool cast_sender_play(
    const cast_device_t *device,
    const char *media_url,
    const char *title,
    int volume,
    char *error,
    size_t error_size);
