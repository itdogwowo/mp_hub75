// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: MIT
//
// C++ bridge — wraps esphome/esp-hub75 Hub75Driver for C callers

#include "hub75_bridge.h"
#include "hub75.h"
#include <sdkconfig.h>
#include <esp_idf_version.h>
#include "soc/gpio_sig_map.h"
#include "soc/lcd_cam_struct.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_private/periph_ctrl.h>
#else
#include <driver/periph_ctrl.h>
#endif
#include <cstdio>
#include <cstdlib>

struct Hub75DriverHandle {
    Hub75Config config;
    Hub75Driver* driver;
    Hub75PixelFormat pixel_format;
    Hub75ColorOrder color_order;
    float refresh_rate;
    uint16_t width;
    uint16_t height;
    bool safe_mode;
    bool started;
    const char* last_stage;
    const char* last_error;
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

static uint32_t get_bit_depth(void) {
#if defined(HUB75_BIT_DEPTH)
    return HUB75_BIT_DEPTH;
#else
    return 8;
#endif
}

static void set_stage(Hub75DriverHandle* h, const char* stage) {
    if (h) {
        h->last_stage = stage;
    }
}

static void set_error(Hub75DriverHandle* h, const char* stage, const char* error) {
    if (h) {
        h->last_stage = stage;
        h->last_error = error;
    }
}

static void clear_error(Hub75DriverHandle* h) {
    if (h) {
        h->last_error = "";
    }
}

static void update_dimensions_from_config(Hub75DriverHandle* h) {
    if (!h) {
        return;
    }
    if (h->config.rotation == Hub75Rotation::ROTATE_90 || h->config.rotation == Hub75Rotation::ROTATE_270) {
        h->width = h->config.panel_height;
        h->height = h->config.panel_width;
    } else {
        h->width = h->config.panel_width;
        h->height = h->config.panel_height;
    }
}

static void reset_pin_to_input(int8_t pin) {
    if (pin < 0) {
        return;
    }
    esp_rom_gpio_pad_select_gpio(pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
}

static void reset_panel_pins(const Hub75Config* c) {
    int8_t pins[] = {
        c->pins.r1, c->pins.g1, c->pins.b1,
        c->pins.r2, c->pins.g2, c->pins.b2,
        c->pins.a, c->pins.b, c->pins.c, c->pins.d, c->pins.e,
        c->pins.lat, c->pins.oe, c->pins.clk,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        reset_pin_to_input(pins[i]);
    }
}

static void preinit_hardware_cleanup(const Hub75Config* c) {
    if (!c) {
        return;
    }

    // Always release GPIO routing before attempting a fresh bring-up.
    reset_panel_pins(c);

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3 uses LCD_CAM + GDMA. A previous run may leave the peripheral in a
    // half-stopped state, so force a conservative reset before reinitializing.
    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    LCD_CAM.lcd_user.lcd_start = 0;
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    LCD_CAM.lcd_user.lcd_reset = 1;
    esp_rom_delay_us(1000);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);
    esp_rom_delay_us(1000);
    periph_module_disable(PERIPH_LCD_CAM_MODULE);
#endif
}

static Hub75Rotation map_rotation(int rotation) {
    if (rotation == 90) {
        return Hub75Rotation::ROTATE_90;
    }
    if (rotation == 180) {
        return Hub75Rotation::ROTATE_180;
    }
    if (rotation == 270) {
        return Hub75Rotation::ROTATE_270;
    }
    return Hub75Rotation::ROTATE_0;
}

static void apply_safe_mode(Hub75BridgeConfig* c) {
    if (!c->safe_mode) {
        return;
    }
    if (c->brightness > 32) {
        c->brightness = 32;
    }
    if (c->clock_speed > 10000000) {
        c->clock_speed = 10000000;
    }
    if (c->min_refresh_rate > 30) {
        c->min_refresh_rate = 30;
    }
    c->clk_phase_inverted = false;
}

// refresh_rate ≈ clock / (N_rows × (2^B - 1) × dma_width)
static float calculate_refresh_rate(const Hub75Config& cfg) {
    uint32_t b = get_bit_depth();
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

    Hub75BridgeConfig cfg = *c;
    apply_safe_mode(&cfg);

    h->config.panel_width  = cfg.panel_width;
    h->config.panel_height = cfg.panel_height;
    h->config.scan_wiring  = map_scan_wiring(cfg.scan_wiring);
    h->config.shift_driver = map_shift_driver(cfg.shift_driver);
    h->config.rotation     = map_rotation(cfg.rotation);
    h->config.output_clock_speed = map_clock_speed(cfg.clock_speed);
    h->config.brightness         = cfg.brightness;
    h->config.double_buffer      = true;       // Always on
    h->config.clk_phase_inverted = cfg.clk_phase_inverted;
    h->config.min_refresh_rate   = cfg.min_refresh_rate;

    // Pins
    h->config.pins.r1 = cfg.r1;  h->config.pins.g1 = cfg.g1;  h->config.pins.b1 = cfg.b1;
    h->config.pins.r2 = cfg.r2;  h->config.pins.g2 = cfg.g2;  h->config.pins.b2 = cfg.b2;
    h->config.pins.a  = cfg.a;   h->config.pins.b  = cfg.b;   h->config.pins.c  = cfg.c;
    h->config.pins.d  = cfg.d;   h->config.pins.e  = cfg.e;
    h->config.pins.lat = cfg.lat; h->config.pins.oe = cfg.oe; h->config.pins.clk = cfg.clk;

    h->pixel_format = map_pixel_format(cfg.pixel_format);
    h->color_order  = Hub75ColorOrder::RGB;
    h->refresh_rate = calculate_refresh_rate(h->config);
    h->safe_mode = cfg.safe_mode;
    h->started = false;
    h->driver = nullptr;
    h->last_stage = "created";
    h->last_error = "";
    update_dimensions_from_config(h);

    return h;
}

void hub75_bridge_close(Hub75DriverHandle* h) {
    if (!h) return;
    set_stage(h, "closing");
    if (h->driver) {
        h->driver->end();
        delete h->driver;
        h->driver = nullptr;
    }
    reset_panel_pins(&h->config);
    h->started = false;
    update_dimensions_from_config(h);
    clear_error(h);
    set_stage(h, "closed");
}

void hub75_bridge_destroy(Hub75DriverHandle* h) {
    if (!h) return;
    hub75_bridge_close(h);
    delete h;
}

bool hub75_bridge_begin(Hub75DriverHandle* h) {
    if (!h) return false;
    clear_error(h);
    if (h->driver || h->started) {
        hub75_bridge_close(h);
        clear_error(h);
    }
    set_stage(h, "preinit_cleanup");
    preinit_hardware_cleanup(&h->config);
    set_stage(h, "allocating_driver");
    h->driver = new Hub75Driver(h->config);
    if (!h->driver) {
        set_error(h, "allocating_driver", "failed to allocate Hub75Driver");
        return false;
    }
    set_stage(h, "starting_driver");
    if (!h->driver->begin()) {
        delete h->driver;
        h->driver = nullptr;
        reset_panel_pins(&h->config);
        set_error(h, "starting_driver", "Hub75Driver::begin() failed");
        return false;
    }
    h->width  = h->driver->get_width();
    h->height = h->driver->get_height();
    h->started = true;
    clear_error(h);
    set_stage(h, "started");
    return true;
}

bool hub75_bridge_show(Hub75DriverHandle* h, const uint8_t* buf, size_t buf_len) {
    if (!h) {
        return false;
    }
    if (!h->driver || !h->started) {
        set_error(h, "show_precheck", "display not started");
        return false;
    }
    if (!buf || buf_len == 0) {
        set_error(h, "show_precheck", "empty buffer");
        return false;
    }
    (void)buf_len;
    clear_error(h);
    set_stage(h, "show_draw_pixels");
    h->driver->draw_pixels(0, 0, h->config.panel_width, h->config.panel_height,
                           buf, h->pixel_format, h->color_order, false);
    set_stage(h, "show_flip_buffer");
    h->driver->flip_buffer();
    set_stage(h, "idle");
    return true;
}

bool hub75_bridge_clear(Hub75DriverHandle* h) {
    if (!h) {
        return false;
    }
    if (!h->driver || !h->started) {
        return true;
    }

    size_t blank_len = (size_t)h->config.panel_width * h->config.panel_height;
    switch (h->pixel_format) {
        case Hub75PixelFormat::RGB888:
            blank_len *= 3;
            break;
        case Hub75PixelFormat::RGB565:
            blank_len *= 2;
            break;
        case Hub75PixelFormat::RGB888_32:
            blank_len *= 4;
            break;
        default:
            blank_len *= 3;
            break;
    }

    uint8_t *blank = (uint8_t *)calloc(blank_len, 1);
    if (!blank) {
        set_error(h, "clear_alloc", "failed to allocate blank frame");
        return false;
    }

    bool ok = hub75_bridge_show(h, blank, blank_len);
    free(blank);
    return ok;
}

void hub75_bridge_set_brightness(Hub75DriverHandle* h, uint8_t v) {
    if (!h) {
        return;
    }
    h->config.brightness = v;
    if (h->driver) {
        h->driver->set_brightness(v);
    }
}

void hub75_bridge_set_rotation(Hub75DriverHandle* h, int r) {
    if (!h) {
        return;
    }
    Hub75Rotation rot = map_rotation(r);
    h->config.rotation = rot;
    if (!h->driver) {
        update_dimensions_from_config(h);
        return;
    }
    h->driver->set_rotation(rot);
    h->width = h->driver->get_width();
    h->height = h->driver->get_height();
}

uint16_t hub75_bridge_get_width(Hub75DriverHandle* h)  { return h ? h->width : 0; }
uint16_t hub75_bridge_get_height(Hub75DriverHandle* h) { return h ? h->height : 0; }
float hub75_bridge_get_refresh_rate(Hub75DriverHandle* h) { return h ? h->refresh_rate : 0.0f; }
bool hub75_bridge_is_running(Hub75DriverHandle* h) { return h && h->driver && h->driver->is_running(); }
uint8_t hub75_bridge_get_brightness(Hub75DriverHandle* h) { return h ? h->config.brightness : 0; }
uint32_t hub75_bridge_get_clock_speed(Hub75DriverHandle* h) { return h ? (uint32_t)h->config.output_clock_speed : 0; }
uint32_t hub75_bridge_get_bit_depth(Hub75DriverHandle* h) { (void)h; return get_bit_depth(); }
bool hub75_bridge_get_safe_mode(Hub75DriverHandle* h) { return h ? h->safe_mode : false; }
const char* hub75_bridge_get_last_stage(Hub75DriverHandle* h) { return h ? h->last_stage : "null_handle"; }
const char* hub75_bridge_get_last_error(Hub75DriverHandle* h) { return h ? h->last_error : "null_handle"; }

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

size_t hub75_bridge_dma_per_buffer(const Hub75BridgeConfig* c) {
    uint32_t dma_w = c->panel_width;
    uint32_t rows = c->panel_height / 2;
    uint32_t bit_depth = get_bit_depth();
    return (size_t)(rows * dma_w * bit_depth * 2);
}

size_t hub75_bridge_dma_overhead(const Hub75BridgeConfig* c) {
    uint32_t rows = c->panel_height / 2;
    uint32_t bit_depth = get_bit_depth();
    uint32_t desc = rows * bit_depth * 12 * 2;
    uint32_t meta = rows * 16 * 2;
    return (size_t)(desc + meta);
}

size_t hub75_bridge_dma_total(const Hub75BridgeConfig* c) {
    size_t per_buf = hub75_bridge_dma_per_buffer(c);
    size_t overhead = hub75_bridge_dma_overhead(c);
    return per_buf * 2 + overhead;
}
