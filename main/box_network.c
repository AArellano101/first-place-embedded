#include "box_network.h"
#include "box_protocol.h"
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define WIFI_READY BIT0
#define WIFI_IP BIT1
#define WIFI_CONNECTING BIT2
static EventGroupHandle_t wifi_events;
static box_config_t provision;
static QueueHandle_t update_queue;
typedef struct { box_response_t body; int64_t started_ms; bool aborted; } request_t;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) xEventGroupSetBits(wifi_events, WIFI_READY);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) xEventGroupClearBits(wifi_events, WIFI_IP | WIFI_CONNECTING);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTING);
        xEventGroupSetBits(wifi_events, WIFI_IP);
        esp_netif_sntp_start(); // TLS certificate dates need wall time, freshness does not.
    }
}
static esp_err_t http_event(esp_http_client_event_t *event)
{
    request_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0)
        box_response_append(&r->body, event->data, (size_t)event->data_len);
    if (event->event_id == HTTP_EVENT_ON_FINISH) r->body.finished = true;
    if (event->event_id == HTTP_EVENT_ON_DATA || event->event_id == HTTP_EVENT_ON_HEADER) {
        int64_t elapsed = esp_timer_get_time() / 1000 - r->started_ms;
        if (!r->aborted && (r->body.rejected || elapsed >= BOX_HTTP_TIMEOUT_MS)) {
            r->aborted = true;
            r->body.rejected = true;
            // Runs inside perform(), on its owning task, not from a watchdog task.
            // Close a trickling/oversized response; never free the handle in a callback.
            esp_http_client_close(event->client);
        } else if (!r->aborted) {
            esp_http_client_set_timeout_ms(event->client, BOX_HTTP_TIMEOUT_MS - (int)elapsed);
        }
    }
    return ESP_OK;
}
static void network_task(void *arg)
{
    (void)arg;
    char url[300];
    if (!box_config_url(&provision, url, sizeof(url))) { vTaskDelete(NULL); return; }
    request_t request = {0};
    const esp_http_client_config_t config = {
        .url = url, .method = HTTP_METHOD_GET, .timeout_ms = BOX_HTTP_TIMEOUT_MS,
        .event_handler = http_event, .user_data = &request,
        .disable_auto_redirect = true, .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 512, .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { ESP_LOGE("box_net", "HTTP allocation failed; lamps remain fail-closed"); vTaskDelete(NULL); return; }
    if (esp_http_client_set_header(client, "X-Device-Key", provision.device_key) != ESP_OK ||
        esp_http_client_set_header(client, "Accept", "application/json") != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGE("box_net", "HTTP header setup failed");
        vTaskDelete(NULL);
        return;
    }
    unsigned failures = 0, connect_failures = 0, successes = 0;
    int64_t next_poll = 0, next_connect = 0, last_time_warning = -60000;
    bool had_ip = false;
    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;
        EventBits_t bits = xEventGroupGetBits(wifi_events);
        if (!(bits & WIFI_IP)) {
            if (had_ip) { ESP_LOGW("box_net", "Wi-Fi disconnected; freshness watchdog remains active"); had_ip = false; }
            if ((bits & WIFI_READY) && !(bits & WIFI_CONNECTING) && now >= next_connect) {
                xEventGroupSetBits(wifi_events, WIFI_CONNECTING);
                esp_err_t err = esp_wifi_connect();
                if (err != ESP_OK) xEventGroupClearBits(wifi_events, WIFI_CONNECTING);
                if (connect_failures < 6) connect_failures++;
                next_connect = now + box_retry_delay(connect_failures, esp_random());
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!had_ip) {
            had_ip = true; connect_failures = 0; next_poll = now;
            ESP_LOGI("box_net", "Wi-Fi ready; polling configured table");
        }
        if (!strncmp(provision.base_url, "https://", 8) && time(NULL) < 1700000000) {
            if (now - last_time_warning >= 60000) {
                ESP_LOGW("box_net", "Waiting for SNTP time before verified HTTPS"); last_time_warning = now;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (now < next_poll) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        box_response_reset(&request.body);
        request.aborted = false;
        request.started_ms = now;
        esp_http_client_set_timeout_ms(client, BOX_HTTP_TIMEOUT_MS);
        esp_err_t result = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        int64_t completed = esp_timer_get_time() / 1000;
        box_light_t light;
        bool valid = box_response_parse(&request.body, result == ESP_OK && !request.aborted,
                                       status, esp_http_client_is_complete_data_received(client), &light);
        if (valid) {
            box_update_t update = {.valid = true, .light = light, .received_ms = completed};
            xQueueOverwrite(update_queue, &update);
            if (failures || !successes) ESP_LOGI("box_net", "Valid state received: %s", box_light_name(light));
            failures = 0;
            successes++;
            if (successes % 60 == 0) ESP_LOGI("box_net", "polls_ok=%u heap_free=%u heap_min=%u",
                successes, (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
            // Approximately 2 seconds start-to-start; do not spin after a slow request.
            next_poll = now + BOX_POLL_MS;
            if (next_poll < completed + 100) next_poll = completed + 100;
        } else {
            if (box_http_config_fault(status)) {
                box_update_t update = {.config_fault = true, .received_ms = completed};
                xQueueOverwrite(update_queue, &update);
                ESP_LOGW("box_net", "HTTP %d: device-key/table/route configuration fault", status);
            } else if (result != ESP_OK) ESP_LOGW("box_net", "Transport/DNS/TLS/timeout failure (code=%d)", (int)result);
            else ESP_LOGW("box_net", "Rejected update: HTTP=%d payload_invalid=%d", status, status == 200);
            // Never log headers, credentials, raw payloads, or request URLs.
            esp_http_client_close(client);
            if (failures < 6) failures++;
            next_poll = completed + box_retry_delay(failures, esp_random());
        }
    }
    // Normal operation is permanent; any future shutdown must run on this task.
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}
esp_err_t box_network_start(const box_config_t *c, QueueHandle_t updates)
{
    if (!box_config_valid(c) || !updates) return ESP_ERR_INVALID_ARG;
    provision = *c;
    update_queue = updates;
    // Do not allow SDK debug logging to reveal SSIDs or HTTP request headers.
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_ERROR);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_events = xEventGroupCreate();
    if (!wifi_events) return ESP_ERR_NO_MEM;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    time_config.start = false;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, c->ssid, strlen(c->ssid));
    memcpy(wifi.sta.password, c->password, strlen(c->password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    memset(&wifi, 0, sizeof(wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    return xTaskCreate(network_task, "box_http", 8192, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
