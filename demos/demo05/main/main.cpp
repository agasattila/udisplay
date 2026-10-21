/*
 * demo05 — uDisplay minimal BLE demo (ESP32 / NimBLE).
 *
 * One button (push_btn) + one LED (status_led).
 * Button click toggles LED; initial LED state is pushed on client_ready.
 *
 * BLE is libudisplay's NimBLE backend (CONFIG_UDISPLAY_ENABLE_NIMBLE, set in
 * sdkconfig.defaults): it owns the GATT server, advertising and connection
 * lifecycle, so this file is the LED and the UI handlers.
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
#include "nvs_flash.h"

#include "libudisplay/udisplay.h"
#include "libudisplay/udisplay_nimble.h"
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

static UDisplay ui;

static uint8_t g_led = 0;

/* ── uDisplay event handlers (assigned to `ui` in app_main) ─────────────────
 *
 * They run in the NimBLE host task with the backend lock held (see
 * libudisplay/udisplay_nimble.h): calling ui.*.set() here needs no extra
 * locking. */

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

    /* The backend drops the link after this returns. */
    ui.on_comms_error = []() {
        ESP_LOGW(TAG, "comms_error: heartbeat timeout");
    };
}

/* ── BLE link (libudisplay NimBLE backend) ─────────────────────────────────── */

static void on_ble_disconnect(int reason, void* userdata)
{
    ESP_LOGI(TAG, "disconnected  reason=%d", reason);
    g_led = 0;
    led_set(false); /* fixes a pre-existing desync bug: g_led reset to 0
                      * here, but nothing updated the physical LED, so it
                      * stayed on across a disconnect until the next
                      * button press or reconnect happened to sync it. */
}

/* ── app_main ────────────────────────────────────────────────────────────── */

extern "C" void app_main(void)
{
    /* NVS is the application's: the BT controller needs it initialized. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_init();

    ui.init(udisplay_nimble_send, UDISPLAY_TRANSPORT_BLE);
    register_ui_handlers();

    udisplay_nimble_config_t ble = {};
    ble.device_name   = "Demo05";
    ble.on_disconnect = on_ble_disconnect;
    ESP_ERROR_CHECK(udisplay_nimble_init(ui.ctx(), &ble));
    ESP_ERROR_CHECK(udisplay_nimble_start());
}
