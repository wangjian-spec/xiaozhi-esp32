/* Implementation of cross-button sequence detector
 */
#include "seq_button.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdio.h>

static const char *TAG = "seq_button";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct reg_button_s {
    button_handle_t btn;
    seq_button_id_t id;
    struct reg_button_s *next;
} reg_button_t;

typedef struct {
    seq_button_id_t id;
    uint32_t time_ms;
} click_rec_t;

static reg_button_t *g_reg_head = NULL;
static seq_event_cb_t g_user_cb = NULL;
static void *g_user_data = NULL;
static uint32_t g_window_ms = 500; // default 500 ms

static click_rec_t g_buffer[32];
static size_t g_buf_len = 0;
static esp_timer_handle_t g_timer = NULL;
static bool g_timer_running = false;

static void seq_timer_cb(void *arg)
{
    // Called from timer task
    seq_event_t evt;
    memset(&evt, 0, sizeof(evt));

    const size_t capacity = sizeof(evt.buttons) / sizeof(evt.buttons[0]);
    seq_event_cb_t cb = NULL;
    void *cb_user = NULL;

    portENTER_CRITICAL(&s_lock);
    size_t len = g_buf_len;
    if (len == 0) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    for (size_t i = 0; i < len && i < capacity; i++) {
        evt.buttons[i] = g_buffer[i].id;
        evt.timestamps_ms[i] = g_buffer[i].time_ms;
    }
    evt.len = (len > capacity) ? capacity : len;
    evt.type = (len > 1) ? SEQ_EVT_SEQUENCE : SEQ_EVT_SINGLE;
    g_buf_len = 0;
    g_timer_running = false;
    cb = g_user_cb;
    cb_user = g_user_data;
    portEXIT_CRITICAL(&s_lock);

    // Log the detected sequence for debugging: print type, length and simple representation
    {
        char seqbuf[32] = {0};
        size_t pos = 0;
        for (size_t i = 0; i < evt.len && pos + 2 < sizeof(seqbuf); ++i) {
            char ch = '?';
            switch (evt.buttons[i]) {
                case SEQ_BTN_UP: ch = 'U'; break;
                case SEQ_BTN_DOWN: ch = 'D'; break;
                case SEQ_BTN_LEFT: ch = 'L'; break;
                case SEQ_BTN_RIGHT: ch = 'R'; break;
                default: ch = '?'; break;
            }
            seqbuf[pos++] = ch;
        }
        seqbuf[pos] = '\0';
        ESP_LOGW(TAG, "seq_button event type=%s len=%d seq=%s",
                 (evt.type == SEQ_EVT_SEQUENCE) ? "SEQ" : "SINGLE",
                 (int)evt.len, seqbuf);
    }

    if (cb) {
        cb(&evt, cb_user);
    }
}

static void internal_button_cb(void *btn_handle, void *usr_data)
{
    (void)usr_data;
    button_handle_t btn = (button_handle_t)btn_handle;
    // find reg id
    seq_button_id_t id = SEQ_BTN_MAX;
    portENTER_CRITICAL(&s_lock);
    for (reg_button_t *p = g_reg_head; p; p = p->next) {
        if (p->btn == btn) {
            id = p->id;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (id == SEQ_BTN_MAX) {
        return; // unknown button
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    portENTER_CRITICAL(&s_lock);
    if (g_buf_len < sizeof(g_buffer)/sizeof(g_buffer[0])) {
        g_buffer[g_buf_len].id = id;
        g_buffer[g_buf_len].time_ms = now_ms;
        g_buf_len++;
    }
    // restart oneshot timer
    if (g_timer) {
        if (g_timer_running) {
            esp_timer_stop(g_timer);
            g_timer_running = false;
        }
        esp_timer_start_once(g_timer, g_window_ms * 1000ULL);
        g_timer_running = true;
    }
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t seq_button_init(uint32_t window_ms)
{
    g_window_ms = window_ms ? window_ms : g_window_ms;

    if (g_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &seq_timer_cb,
            .arg = NULL,
            .name = "seq_timer",
        };
        esp_err_t err = esp_timer_create(&args, &g_timer);
        ESP_RETURN_ON_FALSE(ESP_OK == err, err, TAG, "create timer failed");
    }
    return ESP_OK;
}

esp_err_t seq_button_deinit(void)
{
    // unregister all and free
    portENTER_CRITICAL(&s_lock);
    while (g_reg_head) {
        reg_button_t *p = g_reg_head;
        iot_button_unregister_cb(p->btn, BUTTON_PRESS_UP, NULL);
        g_reg_head = p->next;
        free(p);
    }
    portEXIT_CRITICAL(&s_lock);
    if (g_timer) {
        if (g_timer_running) {
            esp_timer_stop(g_timer);
            g_timer_running = false;
        }
        esp_timer_delete(g_timer);
        g_timer = NULL;
    }
    g_user_cb = NULL;
    g_user_data = NULL;
    g_buf_len = 0;
    return ESP_OK;
}

esp_err_t seq_button_register(button_handle_t btn, seq_button_id_t id)
{
    ESP_RETURN_ON_FALSE(btn, ESP_ERR_INVALID_ARG, TAG, "btn is null");
    ESP_RETURN_ON_FALSE(id < SEQ_BTN_MAX, ESP_ERR_INVALID_ARG, TAG, "id invalid");

    reg_button_t *p = calloc(1, sizeof(reg_button_t));
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "alloc failed");
    p->btn = btn;
    p->id = id;
    portENTER_CRITICAL(&s_lock);
    p->next = g_reg_head;
    g_reg_head = p;
    portEXIT_CRITICAL(&s_lock);

    // register low-level press_up callback
    return iot_button_register_cb(btn, BUTTON_PRESS_UP, NULL, internal_button_cb, NULL);
}

esp_err_t seq_button_unregister(button_handle_t btn)
{
    ESP_RETURN_ON_FALSE(btn, ESP_ERR_INVALID_ARG, TAG, "btn is null");
    portENTER_CRITICAL(&s_lock);
    reg_button_t **pp = &g_reg_head;
    while (*pp) {
        if ((*pp)->btn == btn) {
            reg_button_t *t = *pp;
            *pp = t->next;
            iot_button_unregister_cb(btn, BUTTON_PRESS_UP, NULL);
            free(t);
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
        pp = &(*pp)->next;
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t seq_button_register_handler(seq_event_cb_t cb, void *usr_data)
{
    portENTER_CRITICAL(&s_lock);
    g_user_cb = cb;
    g_user_data = usr_data;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t seq_button_set_window(uint32_t window_ms)
{
    ESP_RETURN_ON_FALSE(window_ms > 0, ESP_ERR_INVALID_ARG, TAG, "window_ms invalid");
    g_window_ms = window_ms;
    return ESP_OK;
}

uint32_t seq_button_get_window(void)
{
    return g_window_ms;
}
