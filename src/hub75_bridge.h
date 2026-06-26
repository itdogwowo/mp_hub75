// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: MIT
//
// C-compatible bridge header for esphome/esp-hub75 C++ driver
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Opaque handle to Hub75Driver + config
typedef struct Hub75DriverHandle Hub75DriverHandle;

// Mirror of Hub75Config with C-compatible types
typedef struct {
    uint16_t panel_width;
    uint16_t panel_height;
    // Pins
    int8_t r1, g1, b1;
    int8_t r2, g2, b2;
    int8_t a, b, c, d, e;
    int8_t lat, oe, clk;
    // Enums (stored as int for C compatibility)
    int pixel_format;      // 0=RGB888, 1=RGB565, 2=RGBX8888
    int shift_driver;      // Hub75ShiftDriver enum value
    int scan_wiring;       // 0=MOD8(1/16 scan), 1=MOD16(1/32 scan)
    int rotation;          // 0/90/180/270
    uint32_t clock_speed;  // Hz (maps to Hub75ClockSpeed)
    uint8_t brightness;    // 0-255
    bool clk_phase_inverted;
    uint16_t min_refresh_rate;  // Usually 60
} Hub75BridgeConfig;

// Lifecycle
Hub75DriverHandle* hub75_bridge_create(const Hub75BridgeConfig* config);
void hub75_bridge_destroy(Hub75DriverHandle* handle);
bool hub75_bridge_begin(Hub75DriverHandle* handle);

// Drawing — converts buf → DMA active buffer → swaps front/active
void hub75_bridge_show(Hub75DriverHandle* handle, const uint8_t* buf, size_t buf_len);

// Control
void hub75_bridge_set_brightness(Hub75DriverHandle* handle, uint8_t brightness);
void hub75_bridge_set_rotation(Hub75DriverHandle* handle, int rotation);

// Query
uint16_t hub75_bridge_get_width(Hub75DriverHandle* handle);
uint16_t hub75_bridge_get_height(Hub75DriverHandle* handle);
float hub75_bridge_get_refresh_rate(Hub75DriverHandle* handle);
bool hub75_bridge_is_running(Hub75DriverHandle* handle);

// Version string (e.g. "esphome/esp-hub75 0.3.6")
const char* hub75_bridge_get_version(void);

// Expected buffer size in bytes for given config and pixel format
size_t hub75_bridge_buf_size(const Hub75BridgeConfig* config);

// Internal DMA memory usage estimate (informational)
size_t hub75_bridge_dma_total(const Hub75BridgeConfig* config);

#ifdef __cplusplus
}
#endif
