#include "backend_adapter.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t invitations;

esp_err_t backend_adapter_init(void)
{
    if (invitations) return ESP_ERR_INVALID_STATE;
    invitations = xQueueCreate(ADAPTER_QUEUE_LENGTH, INVITE_ID_MAX + 1);
    return invitations ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t backend_receive_invitation(const char *id)
{
    if (!invitations) return ESP_ERR_INVALID_STATE;
    if (!id) return ESP_ERR_INVALID_ARG;
    char copy[INVITE_ID_MAX + 1] = {0};
    size_t n = 0;
    while (n <= INVITE_ID_MAX && id[n]) {
        if ((unsigned char)id[n] <= 32 || (unsigned char)id[n] > 126) return ESP_ERR_INVALID_ARG;
        n++;
    }
    if (n > INVITE_ID_MAX) return ESP_ERR_INVALID_ARG;
    memcpy(copy, id, n);
    return xQueueSend(invitations, copy, 0) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t backend_clear_invitation(void) { return backend_receive_invitation(""); }
bool backend_poll_invitation(char id[INVITE_ID_MAX + 1])
{
    return xQueueReceive(invitations, id, 0) == pdPASS;
}

void backend_report_local(const beacon_event_t *event, void *context)
{
    (void)context;
    static const char *names[] = {"availability", "accept", "skip", "timer_start", "timer_cancel", "timer_expire"};
    ESP_LOGI("local_event", "%s availability=%s duration_s=%u invitation_id=%s (local only)",
             names[event->type], beacon_availability_text(event->availability),
             event->duration_s, event->invitation_id[0] ? event->invitation_id : "none");
}

#ifdef CONFIG_THIRD_PLACE_LOCAL_MOCK
static int invite_command(int argc, char **argv)
{
    if (argc != 2) { printf("Usage: invite <id> (1-64 printable ASCII characters, no spaces)\n"); return 1; }
    esp_err_t err = backend_receive_invitation(argv[1]);
    printf("LOCAL MOCK invite enqueue: %s; no server request/confirmation\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}
static int clear_command(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return 1;
    esp_err_t err = backend_clear_invitation();
    printf("LOCAL MOCK clear enqueue: %s\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}
#endif

esp_err_t backend_start_mock_console(void)
{
#ifdef CONFIG_THIRD_PLACE_LOCAL_MOCK
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "local-mock>";
    config.max_cmdline_length = 96;
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart, &config, &repl);
    if (err != ESP_OK) return err;
    const esp_console_cmd_t invite = {.command = "invite", .help = "Queue a LOCAL pending invitation", .func = invite_command};
    const esp_console_cmd_t clear = {.command = "clear_invite", .help = "Clear the LOCAL pending invitation", .func = clear_command};
    if ((err = esp_console_cmd_register(&invite)) != ESP_OK ||
        (err = esp_console_cmd_register(&clear)) != ESP_OK ||
        (err = esp_console_register_help_command()) != ESP_OK ||
        (err = esp_console_start_repl(repl)) != ESP_OK) {
        repl->del(repl);
        return err;
    }
    ESP_LOGI("mock", "Serial invite/clear_invite enabled; no backend integration");
#endif
    return ESP_OK;
}
