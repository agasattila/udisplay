/*
 * demo05 — uDisplay minimal BLE demo (ESP32 / NimBLE).
 *
 * One button (push_btn) + one LED (status_led).
 * Button click toggles LED; initial LED state is pushed on client_ready.
 *
 * LED GPIO/type/polarity are board-specific — see main/Kconfig.projbuild's
 * DEMO05_BOARD choice, selected via `idf.py menuconfig` (not menuconfig ->
 * only the toolchain/target is picked there; the board is a separate choice
 * under "Demo05 Configuration").
 *
 * Flash with:  idf.py -p /dev/ttyUSB0 flash monitor
 */

#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "udisplay.h"
#include "udisplay_ui.hpp"

#if CONFIG_DEMO05_LED_IS_WS2812
#include "led_strip.h"
#endif

#ifndef CONFIG_DEMO05_LED_GPIO
#error "demo05: no DEMO05_BOARD entry exists for this chip target. See main/Kconfig.projbuild's DEMO05_BOARD choice — add a board entry (or override DEMO05_LED_GPIO/IS_WS2812/ACTIVE_LOW manually) before building for this target."
#endif

using namespace udisplay_ui;

#define BUTTON_GPIO GPIO_NUM_9

static const char* TAG = "demo05";

/* ── Board-specific LED control ─────────────────────────────────────────────
 * See main/Kconfig.projbuild's DEMO05_BOARD choice for the per-board
 * GPIO/type/polarity/color defaults this resolves.
 *
 *   CONFIG_DEMO05_BOARD (Kconfig choice, one per depends-on IDF_TARGET_*)
 *            │
 *            ├─ ESP32-C3 Super Mini ──┐
 *            ├─ ESP32 DevKitC ────────┤
 *            ├─ ESP32-S3-DevKitC-1 ───┼──► per-board Kconfig defaults:
 *            ├─ ESP32-C6-DevKitC-1 ───┤       DEMO05_LED_GPIO         (int)
 *            └─ ESP32-H2-DevKitM-1 ───┘       DEMO05_LED_IS_WS2812    (bool)
 *                                             DEMO05_LED_ACTIVE_LOW   (bool, GPIO-only)
 *                                             DEMO05_LED_WS2812_COLOR (hex, WS2812-only)
 *                                                      │
 *                           ┌──────────────────────────┴──────────────────────────┐
 *                           │ #if CONFIG_DEMO05_LED_IS_WS2812                      │
 *                           ▼ (compiled in ONLY for boards that need it)           ▼ #else
 *                 led_init():                                          led_init():
 *                   led_strip_new_rmt_device(...)                        gpio_config(OUTPUT)
 *                     ├─ ESP_OK  → store handle                          led_set(false)
 *                     └─ error   → ESP_LOGE, continue (no                led_set(bool on):
 *                          ESP_ERROR_CHECK abort — the LED is              gpio_set_level(GPIO,
 *                          cosmetic to the demo's real job:                  on ^ ACTIVE_LOW)
 *                          proving BLE connectivity)
 *                 led_set(bool on):
 *                   led_strip_set_pixel(0, on ? WS2812_COLOR : 0)
 *                   led_strip_refresh()
 *
 * Both variants are physical-hardware-only — updating the uDisplay widget
 * (ui.status_led.set()) stays a separate call at each call site (button
 * click, boot, BLE disconnect); led_init()/led_set() know nothing about the
 * widget layer. */
#if CONFIG_DEMO05_LED_IS_WS2812
static led_strip_handle_t g_led_strip = nullptr;
static constexpr uint32_t kLedStripRmtResolutionHz = 10 * 1000 * 1000; /* 10MHz */
#else
/* Kconfig bool macros are only #defined when set to y — CONFIG_DEMO05_LED_ACTIVE_LOW
 * doesn't exist at all on active-high boards, so it can't be used directly as a
 * runtime value. This wrapper is always defined, 0 or 1. */
#if CONFIG_DEMO05_LED_ACTIVE_LOW
#define DEMO05_LED_ACTIVE_LOW_VAL 1
#else
#define DEMO05_LED_ACTIVE_LOW_VAL 0
#endif
#endif

static void led_set(bool on)
{
#if CONFIG_DEMO05_LED_IS_WS2812
    if (!g_led_strip) return; /* init failed earlier — no-op, not a crash */
    if (on) {
        uint8_t r = (CONFIG_DEMO05_LED_WS2812_COLOR >> 16) & 0xFF;
        uint8_t g = (CONFIG_DEMO05_LED_WS2812_COLOR >> 8)  & 0xFF;
        uint8_t b =  CONFIG_DEMO05_LED_WS2812_COLOR        & 0xFF;
        led_strip_set_pixel(g_led_strip, 0, r, g, b);
    } else {
        led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
    }
    led_strip_refresh(g_led_strip);
#else
    gpio_set_level((gpio_num_t)CONFIG_DEMO05_LED_GPIO,
                    on ^ DEMO05_LED_ACTIVE_LOW_VAL);
#endif
}

static void led_init(void)
{
#if CONFIG_DEMO05_LED_IS_WS2812
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = CONFIG_DEMO05_LED_GPIO;
    strip_config.max_leds = 1;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = kLedStripRmtResolutionHz;
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip);
    if (err != ESP_OK) {
        /* RMT channel allocation can fail (e.g. contention with another
         * peripheral); don't take down BLE/uDisplay over a cosmetic LED. */
        ESP_LOGE(TAG, "led_strip init failed (%s) — continuing without LED",
                 esp_err_to_name(err));
        g_led_strip = nullptr;
        return;
    }
#else
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << CONFIG_DEMO05_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led_conf);
    if (err != ESP_OK) {
        /* Same don't-take-down-BLE-over-a-cosmetic-LED posture as the
         * WS2812 branch above — a single valid output pin practically never
         * fails gpio_config, but stay consistent rather than swallow it. */
        ESP_LOGE(TAG, "gpio_config for LED failed (%s) — continuing without LED",
                 esp_err_to_name(err));
        return;
    }
#endif
    led_set(false); /* always start off. Fixes a pre-existing boot bug: the
                      * old raw gpio_set_level(LED_GPIO, 0) call, under this
                      * board's active-low convention, actually turned the
                      * LED ON despite its "// LED off" comment. led_set()
                      * resolves polarity correctly on every board. */
}

/* ── BLE UUIDs (128-bit, NimBLE little-endian byte order) ─────────────────
 * Service: 29825AAA-D882-46F7-A4D6-EA8431AD3455
 * Control: 29825AAA-D882-46F7-A4D6-EA8431AD3456  (WRITE from client)
 * Data:    29825AAA-D882-46F7-A4D6-EA8431AD3457  (NOTIFY to client)
 */
static const ble_uuid128_t g_svc_uuid = BLE_UUID128_INIT(
    0x55, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);
static const ble_uuid128_t g_ctrl_uuid = BLE_UUID128_INIT(
    0x56, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);
static const ble_uuid128_t g_data_uuid = BLE_UUID128_INIT(
    0x57, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);

static UDisplay ui;

static uint16_t g_conn_handle     = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_data_attr_handle = 0;
static uint8_t  g_own_addr_type   = BLE_OWN_ADDR_PUBLIC;
static uint8_t  g_led             = 0;

/* uDisplay's own connected/active state starts only once the client
 * subscribes to notifications (see BLE_GAP_EVENT_SUBSCRIBE below), so the
 * library's bootstrap watchdog has no visibility into the window between
 * radio-level connect and subscribe. g_subscribe_wait_ticks covers that
 * window independently, at the same 3-tick/~15s threshold as the library's
 * own watchdog (UDISPLAY_HB_MISS_MAX). */
static bool     g_handshake_sent      = false;
static uint8_t  g_subscribe_wait_ticks = 0;
#define SUBSCRIBE_WAIT_TICKS_MAX 3u

/* ── uDisplay transport callback ─────────────────────────────────────────── */

static void send_cb(const uint8_t* data, uint16_t len, void* ud)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGW(TAG, "send_cb: mbuf alloc failed");
        return;
    }
    ble_gatts_notify_custom(g_conn_handle, g_data_attr_handle, om);
}

/* ── uDisplay event handlers (assigned to `ui` in app_main) ───────────────── */

static void register_ui_handlers(void)
{
    ui.push_btn.on_click = []() {
        g_led ^= 1u;
        ui.status_led.set((bool)g_led);
        led_set((bool)g_led);
        ESP_LOGI(TAG, "button click → LED %s", g_led ? "ON" : "OFF");
    };

    ui.on_client_ready = []() {
        ui.status_led.set((bool)g_led);
        ESP_LOGI(TAG, "client ready → LED %s", g_led ? "ON" : "OFF");
    };

    ui.on_comms_error = []() {
        ESP_LOGW(TAG, "comms_error: heartbeat timeout");
        if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    };
}

/* ── Heartbeat timer ─────────────────────────────────────────────────────── */

static void heartbeat_timer_cb(TimerHandle_t t)
{
    udisplay_heartbeat();

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE && !g_handshake_sent) {
        if (++g_subscribe_wait_ticks >= SUBSCRIBE_WAIT_TICKS_MAX) {
            ESP_LOGW(TAG, "client never subscribed to notifications — giving up");
            ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
}

/* ── GATT characteristic access callbacks ────────────────────────────────── */

static int ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[UDISPLAY_MAX_MSG_SIZE];
    if (len > sizeof(buf)) return BLE_ATT_ERR_INSUFFICIENT_RES;
    os_mbuf_copydata(ctxt->om, 0, len, buf);
    ui.feed(buf, len);
    return 0;
}

static int data_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    return 0;
}

/* ── GATT service table ──────────────────────────────────────────────────── */

static const struct ble_gatt_svc_def g_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_svc_uuid.u,
        .includes        = NULL,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid         = &g_ctrl_uuid.u,
                .access_cb    = ctrl_access,
                .arg          = NULL,
                .descriptors  = NULL,
                .flags        = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 0,
                .val_handle   = NULL,
                .cpfd         = NULL,
            },
            {
                .uuid         = &g_data_uuid.u,
                .access_cb    = data_access,
                .arg          = NULL,
                .descriptors  = NULL,
                .flags        = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle   = &g_data_attr_handle,
                .cpfd         = NULL,
            },
            {}
        },
    },
    {}
};

/* ── GAP / advertising ───────────────────────────────────────────────────── */

static int gap_event_cb(struct ble_gap_event* ev, void* arg);

static void start_adv(void)
{
    struct ble_hs_adv_fields f = {};
    f.flags             = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name              = (const uint8_t*)"Demo05";
    f.name_len          = 6;
    f.name_is_complete  = 1;
    f.uuids128          = &g_svc_uuid;
    f.num_uuids128      = 1;
    f.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params ap = {};
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &ap, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as \"Demo05\"");
    }
}

static int gap_event_cb(struct ble_gap_event* ev, void* arg)
{
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            g_conn_handle          = ev->connect.conn_handle;
            g_handshake_sent       = false;
            g_subscribe_wait_ticks = 0;
            ESP_LOGI(TAG, "connected  handle=%d", g_conn_handle);
            /* Do NOT send HANDSHAKE here: the client hasn't subscribed to
             * notifications yet, and a notify sent before the peer writes
             * the CCCD is silently dropped by the BLE stack. Deferred to
             * BLE_GAP_EVENT_SUBSCRIBE below. */
        } else {
            start_adv();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected  reason=%d", ev->disconnect.reason);
        g_led                  = 0;
        led_set(false); /* fixes a pre-existing desync bug: g_led reset to 0
                          * here, but nothing updated the physical LED, so it
                          * stayed on across a disconnect until the next
                          * button press or reconnect happened to sync it. */
        g_conn_handle          = BLE_HS_CONN_HANDLE_NONE;
        g_handshake_sent       = false;
        g_subscribe_wait_ticks = 0;
        udisplay_on_disconnect();
        start_adv();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Fires on every CCCD write, including unsubscribe and, on some
         * stacks, a resubscribe mid-connection (e.g. after MTU
         * renegotiation). Gate on notify-enable of the data characteristic,
         * and only send HANDSHAKE once per connection — udisplay_on_connect()
         * resets BLE fragment/reassembly state, so firing it twice would
         * corrupt an in-progress bootstrap. */
        if (ev->subscribe.attr_handle == g_data_attr_handle &&
            ev->subscribe.cur_notify && !g_handshake_sent) {
            g_handshake_sent = true;
            ESP_LOGI(TAG, "client subscribed — sending HANDSHAKE");
            udisplay_on_connect();
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU  conn=%d  mtu=%d",
                 ev->mtu.conn_handle, ev->mtu.value);
        if (!ui.ble_set_mtu(ev->mtu.value - 3u)) {
            ESP_LOGW(TAG, "negotiated MTU rejected (out of range) — keeping previous value");
        }
        break;

    default:
        break;
    }
    return 0;
}

/* ── NimBLE host task ────────────────────────────────────────────────────── */

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }
    start_adv();
}

static void ble_host_task(void* param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ── app_main ────────────────────────────────────────────────────────────── */

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_init();

    ui.init(send_cb, UDISPLAY_TRANSPORT_BLE);
    register_ui_handlers();

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Demo05");

    int rc;
    rc = ble_gatts_count_cfg(g_gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(g_gatt_svcs);
    assert(rc == 0);

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(ble_host_task);

    TimerHandle_t hb = xTimerCreate("hb", pdMS_TO_TICKS(5000), pdTRUE, NULL,
                                    heartbeat_timer_cb);
    if (!hb) {
        ESP_LOGE(TAG, "xTimerCreate failed — heartbeat disabled");
    } else {
        xTimerStart(hb, 0);
    }
}
