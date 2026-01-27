/* Cross-button sequence detector for Up/Down/Left/Right buttons
 * Provides a simple API to register button handles, collect press-up
 * events across buttons, and report either a single click or a multi-button
 * sequence to a caller callback after a configurable time window.
 */
#pragma once

#include "esp_err.h"
#include "iot_button.h"

typedef enum {
    SEQ_BTN_UP = 0,
    SEQ_BTN_DOWN,
    SEQ_BTN_LEFT,
    SEQ_BTN_RIGHT,
    SEQ_BTN_MAX,
} seq_button_id_t;

typedef enum {
    SEQ_EVT_SINGLE = 0,
    SEQ_EVT_SEQUENCE,
} seq_event_type_t;

typedef struct {
    seq_event_type_t type;
    seq_button_id_t buttons[16];
    size_t len;
    uint32_t timestamps_ms[16];
} seq_event_t;

typedef void (*seq_event_cb_t)(const seq_event_t *evt, void *usr_data);

/**
 * Initialize module. `window_ms` is the time window to wait for additional
 * clicks before deciding the collected clicks are a single or a sequence.
 */
esp_err_t seq_button_init(uint32_t window_ms);

/**
 * Deinitialize module and free resources.
 */
esp_err_t seq_button_deinit(void);

/**
 * Register a physical `button_handle_t` with a logical `seq_button_id_t`.
 * The module will internally subscribe to `BUTTON_PRESS_UP` for that handle.
 */
esp_err_t seq_button_register(button_handle_t btn, seq_button_id_t id);

/**
 * Unregister a previously registered button.
 */
esp_err_t seq_button_unregister(button_handle_t btn);

/**
 * Register a handler to receive sequence events.
 */
esp_err_t seq_button_register_handler(seq_event_cb_t cb, void *usr_data);

/**
 * Set/get the sequence detection window (ms).
 */
esp_err_t seq_button_set_window(uint32_t window_ms);
uint32_t  seq_button_get_window(void);
