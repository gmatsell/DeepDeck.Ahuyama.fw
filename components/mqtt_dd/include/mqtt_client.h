#pragma once
/* Stub for esp_mqtt_client — not available in this IDF build */
typedef void *esp_mqtt_client_handle_t;
typedef struct { const char *uri; } esp_mqtt_client_config_t;
static inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *c) { (void)c; return NULL; }
static inline int esp_mqtt_client_start(esp_mqtt_client_handle_t h) { (void)h; return -1; }
static inline int esp_mqtt_client_publish(esp_mqtt_client_handle_t h, const char *t, const char *d, int l, int q, int r) { (void)h;(void)t;(void)d;(void)l;(void)q;(void)r; return -1; }
