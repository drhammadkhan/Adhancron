#include "cast_sender.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "mdns.h"

#define CAST_PORT 8009
#define CAST_RX_CAPACITY (16 * 1024)
#define CAST_TX_CAPACITY 2048
#define CAST_DEFAULT_RECEIVER "CC1AD845"
#define CAST_SOURCE "sender-0"
#define CAST_RECEIVER "receiver-0"
#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define NS_MEDIA "urn:x-cast:com.google.cast.media"

static const char *TAG = "adhan_cast";

typedef struct {
    esp_tls_t *tls;
    uint8_t *receive_buffer;
    uint8_t send_buffer[CAST_TX_CAPACITY];
} cast_connection_t;

typedef struct {
    char namespace_name[96];
    const char *payload;
    size_t payload_length;
} cast_frame_t;

typedef enum {
    CAST_READ_ERROR = -1,
    CAST_READ_TIMEOUT = 0,
    CAST_READ_MESSAGE = 1,
} cast_read_result_t;

typedef struct {
    bool default_receiver_running;
    char transport_id[64];
    char session_id[64];
} receiver_application_t;

static const char *txt_value(const mdns_result_t *result, const char *key) {
    for (size_t index = 0; index < result->txt_count; index++) {
        if (result->txt[index].key != NULL &&
                strcasecmp(result->txt[index].key, key) == 0) {
            return result->txt[index].value;
        }
    }
    return NULL;
}

static bool contains_case_insensitive(const char *text, const char *fragment) {
    if (text == NULL || fragment == NULL) return false;
    const size_t fragment_length = strlen(fragment);
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (strncasecmp(cursor, fragment, fragment_length) == 0) return true;
    }
    return false;
}

size_t cast_sender_discover(cast_device_t *devices, size_t capacity, uint32_t timeout_ms) {
    if (devices == NULL || capacity == 0) return 0;
    mdns_result_t *results = NULL;
    const esp_err_t result = mdns_query_ptr(
        "_googlecast", "_tcp", timeout_ms, capacity * 2, &results);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Cast discovery failed: %s", esp_err_to_name(result));
        return 0;
    }

    size_t count = 0;
    for (mdns_result_t *entry = results;
            entry != NULL && count < capacity; entry = entry->next) {
        esp_ip4_addr_t address = {0};
        bool has_ipv4 = false;
        for (mdns_ip_addr_t *item = entry->addr; item != NULL; item = item->next) {
            if (item->addr.type == ESP_IPADDR_TYPE_V4) {
                address = item->addr.u_addr.ip4;
                has_ipv4 = true;
                break;
            }
        }
        if (!has_ipv4) continue;

        const char *id = txt_value(entry, "id");
        const char *name = txt_value(entry, "fn");
        const char *model = txt_value(entry, "md");
        const char *capabilities = txt_value(entry, "ca");
        const long capability_bits = capabilities ? strtol(capabilities, NULL, 10) : 0;
        if ((capability_bits & 0x100) != 0) continue;

        char host[CAST_DEVICE_HOST_SIZE];
        if (esp_ip4addr_ntoa(&address, host, sizeof(host)) == NULL) continue;
        const char *stable_id = id && id[0] != '\0' ? id : host;
        bool duplicate = false;
        for (size_t previous = 0; previous < count; previous++) {
            if (strcmp(devices[previous].id, stable_id) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        cast_device_t *device = &devices[count++];
        memset(device, 0, sizeof(*device));
        strlcpy(device->id, stable_id, sizeof(device->id));
        strlcpy(device->name,
            name && name[0] != '\0' ? name :
                (entry->instance_name ? entry->instance_name : "Cast device"),
            sizeof(device->name));
        if (model != NULL) strlcpy(device->model, model, sizeof(device->model));
        strlcpy(device->host, host, sizeof(device->host));
        device->port = entry->port != 0 ? entry->port : CAST_PORT;
        device->is_group = (capability_bits & 0x20) != 0 ||
            contains_case_insensitive(model, "group");
    }
    mdns_query_results_free(results);
    ESP_LOGI(TAG, "Discovered %u Cast endpoint%s", (unsigned)count,
        count == 1 ? "" : "s");
    return count;
}

bool cast_sender_find(const char *device_id, cast_device_t *device, uint32_t timeout_ms) {
    if (device_id == NULL || device_id[0] == '\0' || device == NULL) return false;
    const uint32_t attempt_timeout = timeout_ms >= 4000 ? timeout_ms / 2 : timeout_ms;
    const int attempts = timeout_ms >= 4000 ? 2 : 1;
    for (int attempt = 0; attempt < attempts; attempt++) {
        cast_device_t devices[CAST_MAX_DEVICES];
        const size_t count = cast_sender_discover(
            devices, CAST_MAX_DEVICES, attempt_timeout);
        for (size_t index = 0; index < count; index++) {
            if (strcmp(devices[index].id, device_id) == 0) {
                *device = devices[index];
                return true;
            }
        }
        ESP_LOGW(TAG, "Saved Cast receiver did not answer discovery pass %d/%d",
            attempt + 1, attempts);
    }
    return false;
}

static int put_varint(uint8_t *output, size_t capacity, size_t *offset, uint64_t value) {
    do {
        if (*offset >= capacity) return -1;
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        output[(*offset)++] = byte;
    } while (value != 0);
    return 0;
}

static int put_string_field(
        uint8_t *output, size_t capacity, size_t *offset,
        uint8_t field, const char *value) {
    const size_t length = strlen(value);
    if (*offset >= capacity) return -1;
    output[(*offset)++] = (field << 3) | 2;
    if (put_varint(output, capacity, offset, length) != 0 ||
            *offset + length > capacity) return -1;
    memcpy(output + *offset, value, length);
    *offset += length;
    return 0;
}

static int encode_message(
        uint8_t *output, size_t capacity,
        const char *destination, const char *namespace_name,
        const char *payload) {
    size_t offset = 0;
    if (put_varint(output, capacity, &offset, 1 << 3) != 0 ||
            put_varint(output, capacity, &offset, 0) != 0 ||
            put_string_field(output, capacity, &offset, 2, CAST_SOURCE) != 0 ||
            put_string_field(output, capacity, &offset, 3, destination) != 0 ||
            put_string_field(output, capacity, &offset, 4, namespace_name) != 0 ||
            put_varint(output, capacity, &offset, 5 << 3) != 0 ||
            put_varint(output, capacity, &offset, 0) != 0 ||
            put_string_field(output, capacity, &offset, 6, payload) != 0) return -1;
    return (int)offset;
}

static bool get_varint(
        const uint8_t *input, size_t length, size_t *offset, uint64_t *value) {
    uint64_t decoded = 0;
    int shift = 0;
    while (*offset < length && shift <= 63) {
        const uint8_t byte = input[(*offset)++];
        decoded |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *value = decoded;
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool decode_message(
        const uint8_t *input, size_t length, cast_frame_t *frame) {
    memset(frame, 0, sizeof(*frame));
    size_t offset = 0;
    while (offset < length) {
        uint64_t tag = 0;
        if (!get_varint(input, length, &offset, &tag)) return false;
        const int field = tag >> 3;
        const int wire_type = tag & 7;
        if (wire_type == 0) {
            uint64_t ignored = 0;
            if (!get_varint(input, length, &offset, &ignored)) return false;
        } else if (wire_type == 2) {
            uint64_t field_length = 0;
            if (!get_varint(input, length, &offset, &field_length) ||
                    offset + field_length > length) return false;
            if (field == 4) {
                const size_t copy_length = field_length < sizeof(frame->namespace_name) - 1
                    ? (size_t)field_length : sizeof(frame->namespace_name) - 1;
                memcpy(frame->namespace_name, input + offset, copy_length);
                frame->namespace_name[copy_length] = '\0';
            } else if (field == 6) {
                frame->payload = (const char *)input + offset;
                frame->payload_length = field_length;
            }
            offset += field_length;
        } else if (wire_type == 1) {
            if (offset + 8 > length) return false;
            offset += 8;
        } else if (wire_type == 5) {
            if (offset + 4 > length) return false;
            offset += 4;
        } else {
            return false;
        }
    }
    return frame->namespace_name[0] != '\0' && frame->payload != NULL;
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) strlcpy(error, message, error_size);
}

static cast_connection_t *connection_open(const cast_device_t *device) {
    cast_connection_t *connection = calloc(1, sizeof(*connection));
    if (connection == NULL) return NULL;
    connection->receive_buffer = heap_caps_malloc(
        CAST_RX_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (connection->receive_buffer == NULL) {
        connection->receive_buffer = malloc(CAST_RX_CAPACITY);
    }
    connection->tls = esp_tls_init();
    if (connection->receive_buffer == NULL || connection->tls == NULL) {
        if (connection->tls != NULL) esp_tls_conn_destroy(connection->tls);
        free(connection->receive_buffer);
        free(connection);
        return NULL;
    }

    const esp_tls_cfg_t config = {
        .timeout_ms = 8000,
        .skip_common_name = true,
    };
    const int connected = esp_tls_conn_new_sync(
        device->host, strlen(device->host), device->port, &config, connection->tls);
    if (connected != 1) {
        ESP_LOGE(TAG, "TLS connection to %s:%u failed", device->host, device->port);
        esp_tls_conn_destroy(connection->tls);
        free(connection->receive_buffer);
        free(connection);
        return NULL;
    }
    ESP_LOGI(TAG, "Connected to %s at %s:%u", device->name, device->host, device->port);
    return connection;
}

static void connection_close(cast_connection_t *connection) {
    if (connection == NULL) return;
    if (connection->tls != NULL) esp_tls_conn_destroy(connection->tls);
    free(connection->receive_buffer);
    free(connection);
}

static bool connection_send(
        cast_connection_t *connection, const char *destination,
        const char *namespace_name, const char *payload) {
    const int encoded = encode_message(
        connection->send_buffer + 4, sizeof(connection->send_buffer) - 4,
        destination, namespace_name, payload);
    if (encoded < 0) return false;
    connection->send_buffer[0] = encoded >> 24;
    connection->send_buffer[1] = encoded >> 16;
    connection->send_buffer[2] = encoded >> 8;
    connection->send_buffer[3] = encoded;
    const size_t total = encoded + 4;
    size_t sent = 0;
    while (sent < total) {
        const ssize_t count = esp_tls_conn_write(
            connection->tls, connection->send_buffer + sent, total - sent);
        if (count > 0) {
            sent += count;
        } else if (count != ESP_TLS_ERR_SSL_WANT_READ &&
                count != ESP_TLS_ERR_SSL_WANT_WRITE) {
            return false;
        }
    }
    return true;
}

static int read_exact(
        cast_connection_t *connection, uint8_t *output,
        size_t length, int timeout_ms) {
    int socket_fd = -1;
    if (esp_tls_get_conn_sockfd(connection->tls, &socket_fd) == ESP_OK) {
        const struct timeval timeout = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    size_t received = 0;
    while (received < length) {
        const ssize_t count = esp_tls_conn_read(
            connection->tls, output + received, length - received);
        if (count > 0) {
            received += count;
        } else if (count == ESP_TLS_ERR_SSL_WANT_READ ||
                count == ESP_TLS_ERR_SSL_WANT_WRITE) {
            return received == 0 ? 0 : -1;
        } else {
            return -1;
        }
    }
    return 1;
}

static cast_read_result_t connection_receive(
        cast_connection_t *connection, cast_frame_t *frame, int timeout_ms) {
    uint8_t header[4];
    const int header_result = read_exact(connection, header, sizeof(header), timeout_ms);
    if (header_result == 0) return CAST_READ_TIMEOUT;
    if (header_result < 0) return CAST_READ_ERROR;
    const uint32_t length = ((uint32_t)header[0] << 24) |
        ((uint32_t)header[1] << 16) |
        ((uint32_t)header[2] << 8) | header[3];
    if (length == 0 || length > CAST_RX_CAPACITY) return CAST_READ_ERROR;
    if (read_exact(connection, connection->receive_buffer, length, 1500) != 1) {
        return CAST_READ_ERROR;
    }
    return decode_message(connection->receive_buffer, length, frame)
        ? CAST_READ_MESSAGE : CAST_READ_ERROR;
}

static cJSON *frame_json(const cast_frame_t *frame) {
    return cJSON_ParseWithLength(frame->payload, frame->payload_length);
}

static const char *json_string(const cJSON *object, const char *name) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool service_heartbeat(
        cast_connection_t *connection, const cast_frame_t *frame, cJSON *json) {
    if (strcmp(frame->namespace_name, NS_HEARTBEAT) != 0) return false;
    const char *type = json_string(json, "type");
    if (type != NULL && strcmp(type, "PING") == 0) {
        connection_send(connection, CAST_RECEIVER, NS_HEARTBEAT, "{\"type\":\"PONG\"}");
    }
    return true;
}

static bool wait_for_receiver_status(
        cast_connection_t *connection, int timeout_ms,
        bool require_default_receiver,
        receiver_application_t *application, char *error, size_t error_size) {
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        cast_frame_t frame;
        const cast_read_result_t result = connection_receive(connection, &frame, 500);
        if (result == CAST_READ_TIMEOUT) continue;
        if (result == CAST_READ_ERROR) {
            set_error(error, error_size, "Cast connection closed while waiting for the receiver");
            return false;
        }
        cJSON *json = frame_json(&frame);
        if (json == NULL) continue;
        if (service_heartbeat(connection, &frame, json)) {
            cJSON_Delete(json);
            continue;
        }
        const char *type = json_string(json, "type");
        if (type != NULL && (strcmp(type, "LAUNCH_ERROR") == 0 ||
                strcmp(type, "INVALID_REQUEST") == 0)) {
            cJSON_Delete(json);
            set_error(error, error_size, "The Cast receiver rejected the launch request");
            return false;
        }
        if (strcmp(frame.namespace_name, NS_RECEIVER) == 0 &&
                type != NULL && strcmp(type, "RECEIVER_STATUS") == 0) {
            memset(application, 0, sizeof(*application));
            cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
            cJSON *applications = status
                ? cJSON_GetObjectItemCaseSensitive(status, "applications") : NULL;
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, applications) {
                const char *app_id = json_string(item, "appId");
                if (app_id != NULL && strcmp(app_id, CAST_DEFAULT_RECEIVER) == 0) {
                    const char *transport_id = json_string(item, "transportId");
                    const char *session_id = json_string(item, "sessionId");
                    if (transport_id != NULL && session_id != NULL) {
                        application->default_receiver_running = true;
                        strlcpy(application->transport_id, transport_id,
                            sizeof(application->transport_id));
                        strlcpy(application->session_id, session_id,
                            sizeof(application->session_id));
                    }
                    break;
                }
            }
            cJSON_Delete(json);
            if (!require_default_receiver || application->default_receiver_running) {
                return true;
            }
            continue;
        }
        cJSON_Delete(json);
    }
    set_error(error, error_size, "The Cast receiver did not answer in time");
    return false;
}

static bool wait_for_media_start(
        cast_connection_t *connection, int timeout_ms,
        char *error, size_t error_size) {
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline) {
        cast_frame_t frame;
        const cast_read_result_t result = connection_receive(connection, &frame, 500);
        if (result == CAST_READ_TIMEOUT) continue;
        if (result == CAST_READ_ERROR) {
            set_error(error, error_size, "Cast disconnected before playback was confirmed");
            return false;
        }
        cJSON *json = frame_json(&frame);
        if (json == NULL) continue;
        if (service_heartbeat(connection, &frame, json)) {
            cJSON_Delete(json);
            continue;
        }
        const char *type = json_string(json, "type");
        if (type != NULL && (strcmp(type, "LOAD_FAILED") == 0 ||
                strcmp(type, "INVALID_REQUEST") == 0)) {
            cJSON_Delete(json);
            set_error(error, error_size, "The Cast receiver could not load the Adhan MP3");
            return false;
        }
        if (strcmp(frame.namespace_name, NS_MEDIA) == 0 &&
                type != NULL && strcmp(type, "MEDIA_STATUS") == 0) {
            cJSON *statuses = cJSON_GetObjectItemCaseSensitive(json, "status");
            cJSON *status = cJSON_IsArray(statuses)
                ? cJSON_GetArrayItem(statuses, 0) : NULL;
            const char *player_state = status ? json_string(status, "playerState") : NULL;
            if (player_state != NULL &&
                    (strcmp(player_state, "BUFFERING") == 0 ||
                     strcmp(player_state, "PLAYING") == 0)) {
                cJSON_Delete(json);
                return true;
            }
        }
        cJSON_Delete(json);
    }
    set_error(error, error_size, "Cast did not confirm that playback started");
    return false;
}

bool cast_sender_play(
        const cast_device_t *device, const char *media_url, int volume,
        char *error, size_t error_size) {
    if (device == NULL || media_url == NULL || media_url[0] == '\0') {
        set_error(error, error_size, "Cast playback is not configured");
        return false;
    }
    cast_connection_t *connection = connection_open(device);
    if (connection == NULL) {
        set_error(error, error_size, "Could not connect securely to the Cast device");
        return false;
    }

    int request_id = 1;
    bool success = connection_send(
        connection, CAST_RECEIVER, NS_CONNECTION, "{\"type\":\"CONNECT\"}");
    char payload[CAST_TX_CAPACITY / 2];
    snprintf(payload, sizeof(payload),
        "{\"type\":\"SET_VOLUME\",\"volume\":{\"level\":%.3f},\"requestId\":%d}",
        (double)(volume < 0 ? 0 : (volume > 100 ? 100 : volume)) / 100.0,
        request_id++);
    success = success && connection_send(
        connection, CAST_RECEIVER, NS_RECEIVER, payload);
    snprintf(payload, sizeof(payload),
        "{\"type\":\"GET_STATUS\",\"requestId\":%d}", request_id++);
    success = success && connection_send(
        connection, CAST_RECEIVER, NS_RECEIVER, payload);

    receiver_application_t application = {0};
    if (!success || !wait_for_receiver_status(
            connection, 5000, false, &application, error, error_size)) {
        connection_close(connection);
        return false;
    }
    if (!application.default_receiver_running) {
        snprintf(payload, sizeof(payload),
            "{\"type\":\"LAUNCH\",\"appId\":\"%s\",\"requestId\":%d}",
            CAST_DEFAULT_RECEIVER, request_id++);
        if (!connection_send(connection, CAST_RECEIVER, NS_RECEIVER, payload) ||
                !wait_for_receiver_status(
                    connection, 10000, true, &application, error, error_size) ||
                !application.default_receiver_running) {
            if (error != NULL && error[0] == '\0') {
                set_error(error, error_size, "The Default Media Receiver did not start");
            }
            connection_close(connection);
            return false;
        }
    }

    success = connection_send(
        connection, application.transport_id, NS_CONNECTION,
        "{\"type\":\"CONNECT\"}");
    const int written = snprintf(payload, sizeof(payload),
        "{\"type\":\"LOAD\",\"requestId\":%d,\"sessionId\":\"%s\","
        "\"media\":{\"contentId\":\"%s\",\"streamType\":\"BUFFERED\","
        "\"contentType\":\"audio/mpeg\",\"metadata\":{\"metadataType\":3,"
        "\"title\":\"Adhan\",\"artist\":\"Adhancron Prayer Clock\"}},"
        "\"autoplay\":true,\"currentTime\":0,\"customData\":{}}",
        request_id++, application.session_id, media_url);
    if (written < 0 || (size_t)written >= sizeof(payload)) success = false;
    if (success) {
        success = connection_send(
            connection, application.transport_id, NS_MEDIA, payload);
    }
    if (!success) {
        set_error(error, error_size, "Could not send the Adhan to the Cast receiver");
        connection_close(connection);
        return false;
    }

    success = wait_for_media_start(connection, 12000, error, error_size);
    connection_close(connection);
    if (success) {
        ESP_LOGI(TAG, "Cast playback confirmed on %s: %s", device->name, media_url);
    }
    return success;
}
