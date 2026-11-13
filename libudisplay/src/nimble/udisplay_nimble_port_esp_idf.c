// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/* ESP-IDF glue for the NimBLE backend (see udisplay_nimble_port.h). */

#include "udisplay_nimble_port.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char* TAG = "udisplay";

int udisplay_nimble_port_init(void)
{
    /* Since ESP-IDF 5.0 this also initializes the BT controller. NVS must
     * already be initialized: that is the application's job, since it
     * shares the partition. */
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
    }
    return (int)err;
}

static void host_task(void* param)
{
    (void)param;
    nimble_port_run();   /* returns only after nimble_port_stop() */
    nimble_port_freertos_deinit();
}

int udisplay_nimble_port_start(void)
{
    nimble_port_freertos_init(host_task);
    return 0;
}

struct ble_npl_eventq* udisplay_nimble_port_eventq(void)
{
    return nimble_port_get_dflt_eventq();
}
