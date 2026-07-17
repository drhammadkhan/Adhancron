#include "dlna_sender.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DESCRIPTION_CAPACITY (20 * 1024)
#define SOAP_CAPACITY 1536
#define SSDP_PORT 1900
#define SSDP_ADDRESS "239.255.255.250"
#define SSDP_RENDERER_TARGET "urn:schemas-upnp-org:device:MediaRenderer:1"
#define SSDP_SONOS_TARGET "urn:schemas-upnp-org:device:ZonePlayer:1"

static const char *TAG = "adhan_dlna";

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} response_buffer_t;

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        strlcpy(error, message, error_size);
    }
}

static esp_err_t response_event(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }
    response_buffer_t *response = event->user_data;
    if (response == NULL || response->length + event->data_len >= response->capacity) {
        return ESP_FAIL;
    }
    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += event->data_len;
    response->data[response->length] = '\0';
    return ESP_OK;
}

static bool fetch_description(const char *url, char *xml, size_t capacity) {
    response_buffer_t response = {
        .data = xml,
        .capacity = capacity,
        .length = 0,
    };
    xml[0] = '\0';
    const esp_http_client_config_t config = {
        .url = url,
        .event_handler = response_event,
        .user_data = &response,
        .timeout_ms = 6000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return false;
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result == ESP_OK && status >= 200 && status < 300 &&
        response.length > 0;
}

static const char *find_case_insensitive(
        const char *start, const char *end, const char *needle) {
    const size_t length = strlen(needle);
    if (length == 0) return start;
    for (const char *cursor = start;
            cursor + length <= end && *cursor != '\0'; cursor++) {
        if (strncasecmp(cursor, needle, length) == 0) return cursor;
    }
    return NULL;
}

static bool xml_value_range(
        const char *start, const char *end, const char *tag,
        char *output, size_t output_size) {
    char opening[64];
    char closing[64];
    snprintf(opening, sizeof(opening), "<%s>", tag);
    snprintf(closing, sizeof(closing), "</%s>", tag);
    const char *value = find_case_insensitive(start, end, opening);
    if (value == NULL) return false;
    value += strlen(opening);
    const char *finish = find_case_insensitive(value, end, closing);
    if (finish == NULL) return false;
    while (value < finish && isspace((unsigned char)*value)) value++;
    while (finish > value && isspace((unsigned char)finish[-1])) finish--;
    const size_t length = (size_t)(finish - value);
    if (length >= output_size) return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

static bool xml_value(
        const char *xml, const char *tag, char *output, size_t output_size) {
    return xml_value_range(xml, xml + strlen(xml), tag, output, output_size);
}

static bool service_control_url(
        const char *xml, const char *service_name,
        char *output, size_t output_size) {
    const char *end = xml + strlen(xml);
    const char *cursor = xml;
    while ((cursor = find_case_insensitive(cursor, end, "<service>")) != NULL) {
        const char *finish = find_case_insensitive(cursor, end, "</service>");
        if (finish == NULL) break;
        if (find_case_insensitive(cursor, finish, service_name) != NULL &&
                xml_value_range(
                    cursor, finish, "controlURL", output, output_size)) {
            return true;
        }
        cursor = finish + strlen("</service>");
    }
    return false;
}

static bool url_origin(const char *url, char *output, size_t output_size) {
    const char *scheme = strstr(url, "://");
    if (scheme == NULL) return false;
    const char *path = strchr(scheme + 3, '/');
    const size_t length = path == NULL ? strlen(url) : (size_t)(path - url);
    if (length >= output_size) return false;
    memcpy(output, url, length);
    output[length] = '\0';
    return true;
}

static bool resolve_url(
        const char *location, const char *value,
        char *output, size_t output_size) {
    if (strncasecmp(value, "http://", 7) == 0 ||
            strncasecmp(value, "https://", 8) == 0) {
        return strlcpy(output, value, output_size) < output_size;
    }
    char base[DLNA_DEVICE_URL_SIZE];
    if (value[0] == '/') {
        if (!url_origin(location, base, sizeof(base))) return false;
        const int written = snprintf(output, output_size, "%s%s", base, value);
        return written > 0 && (size_t)written < output_size;
    }
    strlcpy(base, location, sizeof(base));
    const char *scheme = strstr(base, "://");
    if (scheme == NULL) return false;
    char *slash = strrchr(base, '/');
    if (slash == NULL || slash < scheme + 3) return false;
    slash[1] = '\0';
    const int written = snprintf(output, output_size, "%s%s", base, value);
    return written > 0 && (size_t)written < output_size;
}

static bool describe_device(
        const char *location, dlna_device_t *device,
        char *error, size_t error_size) {
    char *xml = heap_caps_malloc(
        DESCRIPTION_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (xml == NULL) xml = malloc(DESCRIPTION_CAPACITY);
    if (xml == NULL) {
        set_error(error, error_size, "Not enough memory to read the speaker");
        return false;
    }
    if (!fetch_description(location, xml, DESCRIPTION_CAPACITY)) {
        free(xml);
        set_error(error, error_size, "Could not read the network speaker description");
        return false;
    }

    char av_transport[DLNA_DEVICE_URL_SIZE] = {0};
    char rendering_control[DLNA_DEVICE_URL_SIZE] = {0};
    const bool valid = strstr(xml, "MediaRenderer") != NULL &&
        service_control_url(
            xml, "AVTransport", av_transport, sizeof(av_transport));
    if (!valid) {
        free(xml);
        set_error(error, error_size, "The selected device is not a DLNA media renderer");
        return false;
    }

    memset(device, 0, sizeof(*device));
    strlcpy(device->location, location, sizeof(device->location));
    xml_value(xml, "UDN", device->id, sizeof(device->id));
    xml_value(xml, "friendlyName", device->name, sizeof(device->name));
    xml_value(xml, "modelName", device->model, sizeof(device->model));
    service_control_url(
        xml, "RenderingControl", rendering_control,
        sizeof(rendering_control));
    free(xml);

    if (device->id[0] == '\0') {
        strlcpy(device->id, location, sizeof(device->id));
    }
    if (device->name[0] == '\0') {
        strlcpy(device->name, "DLNA speaker", sizeof(device->name));
    }
    if (!resolve_url(
            location, av_transport, device->av_transport_url,
            sizeof(device->av_transport_url))) {
        set_error(error, error_size, "The speaker returned an invalid control address");
        return false;
    }
    if (rendering_control[0] != '\0') {
        resolve_url(
            location, rendering_control, device->rendering_control_url,
            sizeof(device->rendering_control_url));
    }
    return true;
}

static bool response_location(
        const char *response, char *location, size_t location_size) {
    const char *end = response + strlen(response);
    const char *header = find_case_insensitive(response, end, "\nlocation:");
    if (header == NULL) return false;
    header += strlen("\nlocation:");
    while (*header == ' ' || *header == '\t') header++;
    const char *finish = header;
    while (*finish != '\0' && *finish != '\r' && *finish != '\n') finish++;
    const size_t length = (size_t)(finish - header);
    if (length == 0 || length >= location_size) return false;
    memcpy(location, header, length);
    location[length] = '\0';
    return true;
}

size_t dlna_sender_discover(
        dlna_device_t *devices, size_t capacity, uint32_t timeout_ms) {
    if (devices == NULL || capacity == 0) return 0;
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) return 0;
    const struct timeval receive_timeout = {
        .tv_sec = 0,
        .tv_usec = 250000,
    };
    setsockopt(
        socket_fd, SOL_SOCKET, SO_RCVTIMEO,
        &receive_timeout, sizeof(receive_timeout));
    const struct sockaddr_in destination = {
        .sin_family = AF_INET,
        .sin_port = htons(SSDP_PORT),
        .sin_addr.s_addr = inet_addr(SSDP_ADDRESS),
    };
    const char renderer_request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: " SSDP_ADDRESS ":1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: " SSDP_RENDERER_TARGET "\r\n\r\n";
    const char sonos_request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: " SSDP_ADDRESS ":1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: " SSDP_SONOS_TARGET "\r\n\r\n";
    sendto(
        socket_fd, renderer_request, sizeof(renderer_request) - 1, 0,
        (const struct sockaddr *)&destination, sizeof(destination));
    sendto(
        socket_fd, sonos_request, sizeof(sonos_request) - 1, 0,
        (const struct sockaddr *)&destination, sizeof(destination));

    size_t count = 0;
    const int64_t deadline =
        esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (esp_timer_get_time() < deadline && count < capacity) {
        char response[1536];
        const ssize_t received = recv(
            socket_fd, response, sizeof(response) - 1, 0);
        if (received <= 0) continue;
        response[received] = '\0';
        char location[DLNA_DEVICE_URL_SIZE];
        if (!response_location(response, location, sizeof(location))) continue;
        bool duplicate = false;
        for (size_t index = 0; index < count; index++) {
            if (strcmp(devices[index].location, location) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        char error[96] = {0};
        dlna_device_t device;
        if (!describe_device(location, &device, error, sizeof(error))) continue;
        for (size_t index = 0; index < count; index++) {
            if (strcmp(devices[index].id, device.id) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) devices[count++] = device;
    }
    close(socket_fd);
    ESP_LOGI(TAG, "Discovered %u DLNA renderer%s",
        (unsigned)count, count == 1 ? "" : "s");
    return count;
}

static bool soap_request(
        const char *url, const char *service, const char *action,
        const char *arguments, char *error, size_t error_size) {
    char *body = malloc(SOAP_CAPACITY);
    if (body == NULL) {
        set_error(error, error_size, "Not enough memory for speaker control");
        return false;
    }
    const int written = snprintf(
        body, SOAP_CAPACITY,
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">"
        "%s</u:%s></s:Body></s:Envelope>",
        action, service, arguments, action);
    if (written < 0 || written >= SOAP_CAPACITY) {
        free(body);
        set_error(error, error_size, "The speaker control message is too large");
        return false;
    }
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 6000,
        .buffer_size = 1024,
        .buffer_size_tx = SOAP_CAPACITY,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(body);
        set_error(error, error_size, "Could not create the speaker connection");
        return false;
    }
    char soap_action[160];
    snprintf(
        soap_action, sizeof(soap_action),
        "\"urn:schemas-upnp-org:service:%s:1#%s\"", service, action);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(
        client, "Content-Type", "text/xml; charset=\"utf-8\"");
    esp_http_client_set_header(client, "SOAPACTION", soap_action);
    esp_http_client_set_post_field(client, body, written);
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);
    if (result != ESP_OK || status < 200 || status >= 300) {
        char message[128];
        snprintf(message, sizeof(message),
            "The speaker rejected %s (HTTP %d)", action, status);
        set_error(error, error_size, message);
        return false;
    }
    return true;
}

static bool xml_escape(
        const char *input, char *output, size_t output_size) {
    size_t length = 0;
    for (const char *cursor = input; *cursor != '\0'; cursor++) {
        const char *replacement = NULL;
        if (*cursor == '&') replacement = "&amp;";
        else if (*cursor == '<') replacement = "&lt;";
        else if (*cursor == '>') replacement = "&gt;";
        if (replacement != NULL) {
            const size_t replacement_length = strlen(replacement);
            if (length + replacement_length >= output_size) return false;
            memcpy(output + length, replacement, replacement_length);
            length += replacement_length;
        } else {
            if (length + 1 >= output_size) return false;
            output[length++] = *cursor;
        }
    }
    output[length] = '\0';
    return true;
}

bool dlna_sender_play(
        const char *location, const char *media_url, int volume,
        char *error, size_t error_size) {
    if (location == NULL || location[0] == '\0' ||
            media_url == NULL || media_url[0] == '\0') {
        set_error(error, error_size, "DLNA playback is not configured");
        return false;
    }
    dlna_device_t device;
    if (!describe_device(location, &device, error, error_size)) return false;

    if (device.rendering_control_url[0] != '\0') {
        char volume_arguments[128];
        snprintf(
            volume_arguments, sizeof(volume_arguments),
            "<InstanceID>0</InstanceID><Channel>Master</Channel>"
            "<DesiredVolume>%d</DesiredVolume>",
            volume < 0 ? 0 : (volume > 100 ? 100 : volume));
        char ignored[96] = {0};
        soap_request(
            device.rendering_control_url, "RenderingControl", "SetVolume",
            volume_arguments, ignored, sizeof(ignored));
    }

    char escaped_url[DLNA_DEVICE_URL_SIZE * 2];
    char arguments[DLNA_DEVICE_URL_SIZE * 2 + 128];
    if (!xml_escape(media_url, escaped_url, sizeof(escaped_url))) {
        set_error(error, error_size, "The audio address is too long");
        return false;
    }
    snprintf(
        arguments, sizeof(arguments),
        "<InstanceID>0</InstanceID><CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData></CurrentURIMetaData>",
        escaped_url);
    if (!soap_request(
            device.av_transport_url, "AVTransport", "SetAVTransportURI",
            arguments, error, error_size) ||
            !soap_request(
                device.av_transport_url, "AVTransport", "Play",
                "<InstanceID>0</InstanceID><Speed>1</Speed>",
                error, error_size)) {
        return false;
    }
    ESP_LOGI(TAG, "DLNA playback started on %s: %s", device.name, media_url);
    return true;
}
