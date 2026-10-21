#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void ble_svc_gap_init(void);
int  ble_svc_gap_device_name_set(const char* name);
#ifdef __cplusplus
}
#endif
