// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: MIT
//
// C++ bridge — wraps esphome/esp-hub75 Hub75Driver for C callers

#include "hub75_bridge.h"
#include "hub75.h"
#include <cstdio>

struct Hub75DriverHandle {
    Hub75Config config;
    Hub75Driver* driver;
    Hub75PixelFormat pixel_format;
    Hub75ColorOrder color_order;
    float refresh_rate;
    uint16_t width;
    uint16_t height;
};

// ── Internal helpers ────────────────────────────────────────

static Hub75PixelFormat map_pixel_format(int pf) {
    switch (pf) {
        case 0: return Hub75PixelFormat::RGB888;
        case 1: return Hub75PixelFormat::RGB565;
        case 2: return Hub75PixelFormat::RGB888_32;
        default: return Hub75PixelFormat::RGB888;
    }
}

static Hub75ShiftDriver map_shift_driver(int sd) {
    switch (sd) {
        case 0: return Hub75ShiftDriver::GENERIC;
        case 1: return Hub75ShiftDriver::FM6124;
        case 2: return Hub75ShiftDriver::FM6126A;
        case 3: return Hub75ShiftDriver::ICN2038S;
        case 4: return Hub75ShiftDriver::MBI5124;
        case 5: return Hub75ShiftDriver::DP3246;
        default: return Hub75ShiftDriver::GENERIC;
    }
}

// MOD8 and MOD16 both map to STANDARD_TWO_SCAN.
// The driver auto-detects the actual scan rate from panel_height.
static Hub75ScanWiring map_scan_wiring(int sw) {
    (void)sw;
    return Hub75ScanWiring::STANDARD_TWO_SCAN;
}

static Hub75ClockSpeed map_clock_speed(uint32_t hz) {
    // Try to match known speeds, otherwise round to nearest
    if (hz <= 8000000)  return Hub75ClockSpeed::HZ_8M;
    if (hz <= 10000000) return Hub75ClockSpeed::HZ_10M;
    if (hz <= 16000000) return Hub75ClockSpeed::HZ_16M;
    if (hz <= 18000000) return Hub75ClockSpeed::HZ_18M;
    if (hz <= 20000000) return Hub75ClockSpeed::HZ_20M;
    if (hz <= 23000000) return Hub75ClockSpeed::HZ_23M;
    if (hz <= 27000000) return Hub75ClockSpeed::HZ_27M;
    return Hub75ClockSpeed::HZ_32M;
}

// refresh_rate ≈ clock / (N_rows × (2^B - 1) × dma_width)
static float calculate_refresh_rate(const Hub75Config& cfg) {
    #if defined(HUB75_BIT_DEPTH)
    int b = HUB75_BIT_DEPTH;
    #else
    int b = 8;
    #endif
    uint32_t clk = static_cast<uint32_t>(cfg.output_clock_speed);
    uint32_t dma_w = cfg.panel_width;
    uint32_t rows  = cfg.panel_height / 2;
    uint32_t bcm   = (1u << b) - 1;
    float t = (float)(rows * bcm * dma_w) / (float)clk;
    return (t > 0.0f) ? (1.0f / t) : 0.0f;
}

// Buffer size for user-provided pixel data
static size_t calc_user_buf_size(const Hub75BridgeConfig* c) {
    size_t px = (size_t)c->panel_width * c->panel_height;
    switch (c->pixel_format) {
        case 0: return px * 3;  // RGB888
        case 1: return px * 2;  // RGB565
        case 2: return px * 4;  // RGBX8888
        default: return px * 3;
    }
}

// ── Public API ──────────────────────────────────────────────

Hub75DriverHandle* hub75_bridge_create(const Hub75BridgeConfig* c) {
    auto* h = new Hub75DriverHandle();
    if (!h) return nullptr;

    h->config.panel_width  = c->panel_width;
    h->config.panel_height = c->panel_height;
    h->config.scan_wiring  = map_scan_wiring(c->scan_wiring);
    h->config.shift_driver = map_shift_driver(c->shift_driver);
    h->config.rotation     = (c->rotation == 90)  ? Hub75Rotation::ROTATE_90
                           : (c->rotation == 180) ? Hub75Rotation::ROTATE_180
                           : (c->rotation == 270) ? Hub75Rotation::ROTATE_270
                           :                        Hub75Rotation::ROTATE_0;
    h->config.output_clock_speed = map_clock_speed(c->clock_speed);
    h->config.brightness         = c->brightness;
    h->config.double_buffer      = true;       // Always on
    h->config.clk_phase_inverted = c->clk_phase_inverted;
    h->config.min_refresh_rate   = c->min_refresh_rate;

    // Pins
    h->config.pins.r1 = c->r1;  h->config.pins.g1 = c->g1;  h->config.pins.b1 = c->b1;
    h->config.pins.r2 = c->r2;  h->config.pins.g2 = c->g2;  h->config.pins.b2 = c->b2;
    h->config.pins.a  = c->a;   h->config.pins.b  = c->b;   h->config.pins.c  = c->c;
    h->config.pins.d  = c->d;   h->config.pins.e  = c->e;
    h->config.pins.lat = c->lat; h->config.pins.oe = c->oe; h->config.pins.clk = c->clk;

    h->pixel_format = map_pixel_format(c->pixel_format);
    h->color_order  = Hub75ColorOrder::RGB;
    h->refresh_rate = calculate_refresh_rate(h->config);
    h->width  = c->panel_width;
    h->height = c->panel_height;
    h->driver = nullptr;

    return h;
}

void hub75_bridge_destroy(Hub75DriverHandle* h) {
    if (!h) return;
    if (h->driver) {
        h->driver->end();
        delete h->driver;
        h->driver = nullptr;
    }
    delete h;
}

bool hub75_bridge_begin(Hub75DriverHandle* h) {
    if (!h) return false;
    h->driver = new Hub75Driver(h->config);
    if (!h->driver || !h->driver->begin()) {
        delete h->driver;
        h->driver = nullptr;
        return false;
    }
    h->width  = h->driver->get_width();
    h->height = h->driver->get_height();
    return true;
}

void hub75_bridge_show(Hub75DriverHandle* h, const uint8_t* buf, size_t buf_len) {
    if (!h || !h->driver || !buf) return;
    (void)buf_len;
    h->driver->draw_pixels(0, 0, h->config.panel_width, h->config.panel_height,
                           buf, h->pixel_format, h->color_order, false);
    h->driver->flip_buffer();
}

void hub75_bridge_set_brightness(Hub75DriverHandle* h, uint8_t v) {
    if (h && h->driver) h->driver->set_brightness(v);
}

void hub75_bridge_set_rotation(Hub75DriverHandle* h, int r) {
    if (!h || !h->driver) return;
    Hub75Rotation rot = (r == 90)  ? Hub75Rotation::ROTATE_90
                      : (r == 180) ? Hub75Rotation::ROTATE_180
                      : (r == 270) ? Hub75Rotation::ROTATE_270
                      :              Hub75Rotation::ROTATE_0;
    h->driver->set_rotation(rot);
    h->width  = h->driver->get_width();
    h->height = h->driver->get_height();
}

uint16_t hub75_bridge_get_width(Hub75DriverHandle* h)  { return h ? h->width : 0; }
uint16_t hub75_bridge_get_height(Hub75DriverHandle* h) { return h ? h->height : 0; }
float hub75_bridge_get_refresh_rate(Hub75DriverHandle* h) { return h ? h->refresh_rate : 0.0f; }
bool hub75_bridge_is_running(Hub75DriverHandle* h) { return h && h->driver && h->driver->is_running(); }

const char* hub75_bridge_get_version(void) {
#ifdef MP_HUB75_DRIVER_VERSION
    return MP_HUB75_DRIVER_VERSION;
#else
    return "unknown";
#endif
}

size_t hub75_bridge_buf_size(const Hub75BridgeConfig* c) {
    return calc_user_buf_size(c);
}

size_t hub75_bridge_dma_total(const Hub75BridgeConfig* c) {
    // Estimate: 2 × N_rows × dma_width × bit_depth × 2 + overhead
    uint32_t dma_w    = c->panel_width;
    uint32_t rows     = c->panel_height / 2;
    uint32_t per_buf  = rows * dma_w * 8 * 2;      // bit_depth=8, uint16_t=2 bytes
    uint32_t desc     = rows * 8 * 12 * 2;          // descriptors × 2 buffers
    uint32_t meta     = rows * 16 * 2;              // metadata × 2 buffers
    return (size_t)(per_buf * 2 + desc + meta);
}
