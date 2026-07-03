// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: MIT
//
// MicroPython C module for HUB75 RGB LED matrix display
// Wraps esphome/esp-hub75 via C++ bridge layer

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "hub75_bridge.h"

#include <string.h>

// ── Enum constants ──────────────────────────────────────────

#define PF_RGB888    0
#define PF_RGB565    1
#define PF_RGBX8888  2

#define DRV_GENERIC  0
#define DRV_FM6124   1
#define DRV_FM6126A  2
#define DRV_ICN2038S 3
#define DRV_MBI5124  4
#define DRV_DP3246   5

#define SCAN_MOD8  0
#define SCAN_MOD16 1

#define ROT_0   0
#define ROT_90  90
#define ROT_180 180
#define ROT_270 270

#define CLK_8M  8000000
#define CLK_10M 10000000
#define CLK_16M 16000000
#define CLK_18M 18000000
#define CLK_20M 20000000
#define CLK_23M 23000000
#define CLK_27M 27000000
#define CLK_32M 32000000

// ── Display object type ─────────────────────────────────────

extern const mp_obj_type_t mp_hub75_display_type;

typedef struct _hub75_display_obj_t {
    mp_obj_base_t base;
    Hub75DriverHandle* handle;
    int pixel_format;
    uint16_t width;
    uint16_t height;
    bool running;
} hub75_display_obj_t;

static Hub75DriverHandle *s_shared_handle = NULL;
static Hub75BridgeConfig s_shared_cfg = {0};
static bool s_shared_cfg_valid = false;

// ── Display.make_new ────────────────────────────────────────

enum {
    ARG_width, ARG_height, ARG_r1, ARG_g1, ARG_b1,
    ARG_r2, ARG_g2, ARG_b2,
    ARG_a, ARG_b, ARG_c, ARG_d, ARG_e,
    ARG_lat, ARG_oe, ARG_clk,
    ARG_pixel_format, ARG_driver, ARG_scan,
    ARG_brightness, ARG_rotation, ARG_clock_speed,
    ARG_clk_phase_inverted, ARG_min_refresh_rate, ARG_safe_mode,
};

static const mp_arg_t display_allowed_args[] = {
    { MP_QSTR_width,       MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
    { MP_QSTR_height,      MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
    { MP_QSTR_r1,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_g1,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_b1,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_r2,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_g2,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_b2,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_a,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_b,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_c,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_d,   MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_e,   MP_ARG_KW_ONLY | MP_ARG_INT,   {.u_int = -1} },
    { MP_QSTR_lat, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_oe,  MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_clk, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = -1} },
    { MP_QSTR_pixel_format, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = PF_RGB888} },
    { MP_QSTR_driver,       MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DRV_GENERIC} },
    { MP_QSTR_scan,         MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = SCAN_MOD8} },
    { MP_QSTR_brightness,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 128} },
    { MP_QSTR_rotation,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = ROT_0} },
    { MP_QSTR_clock_speed,  MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = CLK_20M} },
    { MP_QSTR_clk_phase_inverted, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    { MP_QSTR_min_refresh_rate,   MP_ARG_KW_ONLY | MP_ARG_INT,  {.u_int = 60} },
    { MP_QSTR_safe_mode,          MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
};

static const char *display_last_stage(hub75_display_obj_t *self) {
    if (!self || !self->handle) {
        return "released";
    }
    const char *stage = hub75_bridge_get_last_stage(self->handle);
    return (stage && stage[0]) ? stage : "unknown";
}

static const char *display_last_error(hub75_display_obj_t *self) {
    if (!self || !self->handle) {
        return "released";
    }
    const char *error = hub75_bridge_get_last_error(self->handle);
    return (error && error[0]) ? error : "none";
}

static void display_raise_bridge_error(hub75_display_obj_t *self, const char *action) {
    mp_raise_msg_varg(&mp_type_RuntimeError,
        MP_ERROR_TEXT("%s failed at %s: %s"),
        action, display_last_stage(self), display_last_error(self));
}

static void display_validate_pins(const Hub75BridgeConfig *cfg) {
    static const char *pin_names[] = {
        "r1", "g1", "b1", "r2", "g2", "b2",
        "a", "b", "c", "d", "lat", "oe", "clk"
    };
    int8_t required_pins[] = {
        cfg->r1, cfg->g1, cfg->b1, cfg->r2, cfg->g2, cfg->b2,
        cfg->a, cfg->b, cfg->c, cfg->d, cfg->lat, cfg->oe, cfg->clk
    };

    for (size_t i = 0; i < MP_ARRAY_SIZE(required_pins); ++i) {
        if (required_pins[i] < 0) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("pin '%s' is required"),
                pin_names[i]);
        }
    }

    int8_t pins[] = {
        cfg->r1, cfg->g1, cfg->b1, cfg->r2, cfg->g2, cfg->b2,
        cfg->a, cfg->b, cfg->c, cfg->d, cfg->e, cfg->lat, cfg->oe, cfg->clk
    };
    const char *all_pin_names[] = {
        "r1", "g1", "b1", "r2", "g2", "b2",
        "a", "b", "c", "d", "e", "lat", "oe", "clk"
    };

    for (size_t i = 0; i < MP_ARRAY_SIZE(pins); ++i) {
        if (pins[i] < 0) {
            continue;
        }
        for (size_t j = i + 1; j < MP_ARRAY_SIZE(pins); ++j) {
            if (pins[i] == pins[j]) {
                mp_raise_msg_varg(&mp_type_ValueError,
                    MP_ERROR_TEXT("pins '%s' and '%s' both use GPIO %d"),
                    all_pin_names[i], all_pin_names[j], (int)pins[i]);
            }
        }
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if MICROPY_HW_USB_CDC
    for (size_t i = 0; i < MP_ARRAY_SIZE(pins); ++i) {
        if (pins[i] == 19 || pins[i] == 20) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("GPIO %d is reserved for native USB CDC on ESP32-S3; use another pin or disable USB CDC"),
                (int)pins[i]);
        }
    }
#endif
#endif
}

static void display_validate_config(const Hub75BridgeConfig *cfg) {
    if (!(cfg->pixel_format == PF_RGB888 || cfg->pixel_format == PF_RGB565 || cfg->pixel_format == PF_RGBX8888)) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid pixel_format"));
    }
    if (cfg->shift_driver < DRV_GENERIC || cfg->shift_driver > DRV_DP3246) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid driver"));
    }
    if (!(cfg->scan_wiring == SCAN_MOD8 || cfg->scan_wiring == SCAN_MOD16)) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid scan"));
    }
    if (!(cfg->rotation == ROT_0 || cfg->rotation == ROT_90 || cfg->rotation == ROT_180 || cfg->rotation == ROT_270)) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("rotation must be 0/90/180/270"));
    }
    if (cfg->clock_speed == 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("clock_speed must be > 0"));
    }
    if (cfg->min_refresh_rate == 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("min_refresh_rate must be > 0"));
    }
    display_validate_pins(cfg);
}

static bool display_same_config(const Hub75BridgeConfig *a, const Hub75BridgeConfig *b) {
    return a->panel_width == b->panel_width &&
        a->panel_height == b->panel_height &&
        a->r1 == b->r1 && a->g1 == b->g1 && a->b1 == b->b1 &&
        a->r2 == b->r2 && a->g2 == b->g2 && a->b2 == b->b2 &&
        a->a == b->a && a->b == b->b && a->c == b->c &&
        a->d == b->d && a->e == b->e &&
        a->lat == b->lat && a->oe == b->oe && a->clk == b->clk &&
        a->pixel_format == b->pixel_format &&
        a->shift_driver == b->shift_driver &&
        a->scan_wiring == b->scan_wiring &&
        a->rotation == b->rotation &&
        a->clock_speed == b->clock_speed &&
        a->clk_phase_inverted == b->clk_phase_inverted &&
        a->min_refresh_rate == b->min_refresh_rate &&
        a->safe_mode == b->safe_mode;
}

static void display_register_shared(Hub75DriverHandle *handle, const Hub75BridgeConfig *cfg) {
    s_shared_handle = handle;
    s_shared_cfg = *cfg;
    s_shared_cfg_valid = true;
}

static void display_clear_shared(Hub75DriverHandle *handle) {
    if (s_shared_handle == handle) {
        s_shared_handle = NULL;
        s_shared_cfg_valid = false;
        memset(&s_shared_cfg, 0, sizeof(s_shared_cfg));
    }
}

static void display_release(hub75_display_obj_t *self);
static void display_destroy(hub75_display_obj_t *self);

static mp_obj_t display_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_val_t parsed[MP_ARRAY_SIZE(display_allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(display_allowed_args), display_allowed_args, parsed);

    hub75_display_obj_t *self = mp_obj_malloc_with_finaliser(hub75_display_obj_t, &mp_hub75_display_type);
    self->handle = NULL;
    self->running = false;

    int w = parsed[ARG_width].u_int;
    int h = parsed[ARG_height].u_int;
    if (w <= 0 || h <= 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("width and height must be > 0"));
    }

    Hub75BridgeConfig cfg = {0};
    cfg.panel_width  = (uint16_t)w;
    cfg.panel_height = (uint16_t)h;

    // Pins
    cfg.r1 = (int8_t)parsed[ARG_r1].u_int;
    cfg.g1 = (int8_t)parsed[ARG_g1].u_int;
    cfg.b1 = (int8_t)parsed[ARG_b1].u_int;
    cfg.r2 = (int8_t)parsed[ARG_r2].u_int;
    cfg.g2 = (int8_t)parsed[ARG_g2].u_int;
    cfg.b2 = (int8_t)parsed[ARG_b2].u_int;
    cfg.a  = (int8_t)parsed[ARG_a].u_int;
    cfg.b  = (int8_t)parsed[ARG_b].u_int;
    cfg.c  = (int8_t)parsed[ARG_c].u_int;
    cfg.d  = (int8_t)parsed[ARG_d].u_int;
    cfg.e  = (int8_t)parsed[ARG_e].u_int;
    cfg.lat = (int8_t)parsed[ARG_lat].u_int;
    cfg.oe  = (int8_t)parsed[ARG_oe].u_int;
    cfg.clk = (int8_t)parsed[ARG_clk].u_int;

    int brightness = parsed[ARG_brightness].u_int;
    int rotation = parsed[ARG_rotation].u_int;
    int clock_speed = parsed[ARG_clock_speed].u_int;
    int min_refresh_rate = parsed[ARG_min_refresh_rate].u_int;
    if (brightness < 0 || brightness > 255) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("brightness 0-255"));
    }
    if (clock_speed <= 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("clock_speed must be > 0"));
    }
    if (min_refresh_rate <= 0 || min_refresh_rate > 65535) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("min_refresh_rate must be 1-65535"));
    }

    // Optional
    cfg.pixel_format  = parsed[ARG_pixel_format].u_int;
    cfg.shift_driver  = parsed[ARG_driver].u_int;
    cfg.scan_wiring   = parsed[ARG_scan].u_int;
    cfg.brightness    = (uint8_t)brightness;
    cfg.rotation      = rotation;
    cfg.clock_speed   = (uint32_t)clock_speed;
    cfg.clk_phase_inverted = parsed[ARG_clk_phase_inverted].u_bool;
    cfg.min_refresh_rate = (uint16_t)min_refresh_rate;
    cfg.safe_mode = parsed[ARG_safe_mode].u_bool;

    display_validate_config(&cfg);

    self->pixel_format = cfg.pixel_format;
    self->width  = w;
    self->height = h;

    if (s_shared_handle) {
        if (!s_shared_cfg_valid || !display_same_config(&cfg, &s_shared_cfg)) {
            mp_raise_msg(&mp_type_RuntimeError,
                MP_ERROR_TEXT("display already initialized with different config; hard reset required"));
        }
        self->handle = s_shared_handle;
        self->width = hub75_bridge_get_width(self->handle);
        self->height = hub75_bridge_get_height(self->handle);
        self->running = hub75_bridge_is_running(self->handle);
        if (self->running) {
            // After soft reboot, reuse the live hardware instance but clear any
            // previously scanned frame to avoid a brief flash of stale content.
            hub75_bridge_clear(self->handle);
        }
    } else {
        self->handle = hub75_bridge_create(&cfg);
        if (!self->handle) {
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to create HUB75 driver"));
        }
        display_register_shared(self->handle, &cfg);
    }

    return MP_OBJ_FROM_PTR(self);
}

// ── Display.begin ───────────────────────────────────────────

static mp_obj_t display_begin(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->handle) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display released. Hard reset required."));
    }
    if (hub75_bridge_is_running(self->handle)) {
        self->width  = hub75_bridge_get_width(self->handle);
        self->height = hub75_bridge_get_height(self->handle);
        self->running = true;
        return mp_const_true;
    }
    if (self->running) return mp_const_true;
    if (!hub75_bridge_begin(self->handle)) {
        Hub75BridgeConfig cfg = {0};
        cfg.panel_width  = self->width;
        cfg.panel_height = self->height;
        size_t needed = hub75_bridge_dma_total(&cfg);
        mp_raise_msg_varg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("begin failed at %s: %s (~%u DMA bytes)"),
            display_last_stage(self), display_last_error(self), (unsigned int)needed);
    }
    self->width  = hub75_bridge_get_width(self->handle);
    self->height = hub75_bridge_get_height(self->handle);
    self->running = true;
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_begin_obj, display_begin);

// ── Display.show(buf) ───────────────────────────────────────

static mp_obj_t display_show(mp_obj_t self_in, mp_obj_t buf_obj) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->running) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Display not started. Call begin() first."));
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);

    size_t expected = (size_t)self->width * self->height;
    switch (self->pixel_format) {
        case PF_RGB888:   expected *= 3; break;
        case PF_RGB565:   expected *= 2; break;
        case PF_RGBX8888: expected *= 4; break;
    }
    if (bufinfo.len < expected) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("Buffer too small: got %u bytes, expected %u"),
            (unsigned int)bufinfo.len, (unsigned int)expected);
    }

    if (!hub75_bridge_show(self->handle, (const uint8_t*)bufinfo.buf, bufinfo.len)) {
        display_raise_bridge_error(self, "show");
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(display_show_obj, display_show);

// ── set_brightness / set_rotation ───────────────────────────

static mp_obj_t display_set_brightness(mp_obj_t self_in, mp_obj_t val_obj) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int v = mp_obj_get_int(val_obj);
    if (v < 0 || v > 255) mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("brightness 0-255"));
    hub75_bridge_set_brightness(self->handle, (uint8_t)v);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(display_set_brightness_obj, display_set_brightness);

static mp_obj_t display_set_rotation(mp_obj_t self_in, mp_obj_t val_obj) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int r = mp_obj_get_int(val_obj);
    hub75_bridge_set_rotation(self->handle, r);
    self->width  = hub75_bridge_get_width(self->handle);
    self->height = hub75_bridge_get_height(self->handle);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(display_set_rotation_obj, display_set_rotation);

static mp_obj_t display_close(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle) {
        Hub75DriverHandle *handle = self->handle;
        hub75_bridge_destroy(handle);
        display_clear_shared(handle);
        self->handle = NULL;
    }
    self->running = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_close_obj, display_close);

static mp_obj_t display_status(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t dict = mp_obj_new_dict(10);
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_width), mp_obj_new_int(self->width));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_height), mp_obj_new_int(self->height));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_refresh_rate), mp_obj_new_float(hub75_bridge_get_refresh_rate(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_is_running), mp_obj_new_bool(hub75_bridge_is_running(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_brightness), mp_obj_new_int(hub75_bridge_get_brightness(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_clock_speed), mp_obj_new_int_from_uint(hub75_bridge_get_clock_speed(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_bit_depth), mp_obj_new_int_from_uint(hub75_bridge_get_bit_depth(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_safe_mode), mp_obj_new_bool(hub75_bridge_get_safe_mode(self->handle)));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_last_stage), mp_obj_new_str(display_last_stage(self), strlen(display_last_stage(self))));
    mp_obj_dict_store(dict, MP_ROM_QSTR(MP_QSTR_last_error), mp_obj_new_str(display_last_error(self), strlen(display_last_error(self))));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_status_obj, display_status);

// ── Attributes (width, height, refresh_rate, is_running) ───

static void display_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR_width) {
            dest[0] = mp_obj_new_int(self->width);
        } else if (attr == MP_QSTR_height) {
            dest[0] = mp_obj_new_int(self->height);
        } else if (attr == MP_QSTR_refresh_rate) {
            dest[0] = mp_obj_new_float(hub75_bridge_get_refresh_rate(self->handle));
        } else if (attr == MP_QSTR_is_running) {
            dest[0] = mp_obj_new_bool(hub75_bridge_is_running(self->handle));
        } else if (attr == MP_QSTR_brightness) {
            dest[0] = mp_obj_new_int(hub75_bridge_get_brightness(self->handle));
        } else if (attr == MP_QSTR_clock_speed) {
            dest[0] = mp_obj_new_int_from_uint(hub75_bridge_get_clock_speed(self->handle));
        } else if (attr == MP_QSTR_bit_depth) {
            dest[0] = mp_obj_new_int_from_uint(hub75_bridge_get_bit_depth(self->handle));
        } else if (attr == MP_QSTR_safe_mode) {
            dest[0] = mp_obj_new_bool(hub75_bridge_get_safe_mode(self->handle));
        } else if (attr == MP_QSTR_last_stage) {
            dest[0] = mp_obj_new_str(display_last_stage(self), strlen(display_last_stage(self)));
        } else if (attr == MP_QSTR_last_error) {
            dest[0] = mp_obj_new_str(display_last_error(self), strlen(display_last_error(self)));
        } else {
            // Fall through to locals_dict for bound methods.
            dest[1] = MP_OBJ_SENTINEL;
        }
    }
}

// ── buffer_info() ───────────────────────────────────────────

static mp_obj_t display_buffer_info(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    Hub75BridgeConfig cfg = {0};
    cfg.panel_width = self->width;
    cfg.panel_height = self->height;
    size_t per = hub75_bridge_dma_per_buffer(&cfg);
    size_t ovh = hub75_bridge_dma_overhead(&cfg);
    size_t total = hub75_bridge_dma_total(&cfg);
    mp_obj_t items[4];
    items[0] = mp_obj_new_int_from_uint((mp_uint_t)per);
    items[1] = mp_obj_new_int_from_uint((mp_uint_t)(per * 2));
    items[2] = mp_obj_new_int_from_uint((mp_uint_t)ovh);
    items[3] = mp_obj_new_int_from_uint((mp_uint_t)total);
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_buffer_info_obj, display_buffer_info);

// ── __del__ ─────────────────────────────────────────────────

static void display_release(hub75_display_obj_t *self) {
    if (self->handle) {
        self->width = hub75_bridge_get_width(self->handle);
        self->height = hub75_bridge_get_height(self->handle);
        self->running = hub75_bridge_is_running(self->handle);
        return;
    }
    self->running = false;
}

static void display_destroy(hub75_display_obj_t *self) {
    self->handle = NULL;
    self->running = false;
}

static mp_obj_t display_del(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    display_destroy(self);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_del_obj, display_del);

static mp_obj_t display_exit(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    display_release(self);
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(display_exit_obj, 4, 4, display_exit);

// ── Display locals_dict ─────────────────────────────────────

static const mp_rom_map_elem_t display_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_begin),          MP_ROM_PTR(&display_begin_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),           MP_ROM_PTR(&display_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_close),          MP_ROM_PTR(&display_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),         MP_ROM_PTR(&display_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_brightness), MP_ROM_PTR(&display_set_brightness_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_rotation),   MP_ROM_PTR(&display_set_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_buffer_info),    MP_ROM_PTR(&display_buffer_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_status),         MP_ROM_PTR(&display_status_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__),        MP_ROM_PTR(&display_del_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__),      MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__),       MP_ROM_PTR(&display_exit_obj) },
};
static MP_DEFINE_CONST_DICT(display_locals_dict, display_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_hub75_display_type,
    MP_QSTR_Display,
    MP_TYPE_FLAG_NONE,
    make_new, display_make_new,
    attr, display_attr,
    locals_dict, &display_locals_dict
);

// ── Module-level: version() ─────────────────────────────────

#ifndef MP_HUB75_DRIVER_VERSION
#define MP_HUB75_DRIVER_VERSION "unknown"
#endif

static mp_obj_t mp_hub75_version(void) {
    return mp_obj_new_str(MP_HUB75_DRIVER_VERSION, strlen(MP_HUB75_DRIVER_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_hub75_version_obj, mp_hub75_version);

// ── Module globals table ────────────────────────────────────

static const mp_rom_map_elem_t hub75_module_globals_table[] = {
    // Class
    { MP_ROM_QSTR(MP_QSTR_Display), MP_ROM_PTR(&mp_hub75_display_type) },

    // Function
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mp_hub75_version_obj) },

    // PixelFormat
    { MP_ROM_QSTR(MP_QSTR_RGB888),   MP_ROM_INT(PF_RGB888) },
    { MP_ROM_QSTR(MP_QSTR_RGB565),   MP_ROM_INT(PF_RGB565) },
    { MP_ROM_QSTR(MP_QSTR_RGBX8888), MP_ROM_INT(PF_RGBX8888) },

    // Driver
    { MP_ROM_QSTR(MP_QSTR_GENERIC),  MP_ROM_INT(DRV_GENERIC) },
    { MP_ROM_QSTR(MP_QSTR_FM6124),   MP_ROM_INT(DRV_FM6124) },
    { MP_ROM_QSTR(MP_QSTR_FM6126A),  MP_ROM_INT(DRV_FM6126A) },
    { MP_ROM_QSTR(MP_QSTR_ICN2038S), MP_ROM_INT(DRV_ICN2038S) },
    { MP_ROM_QSTR(MP_QSTR_MBI5124),  MP_ROM_INT(DRV_MBI5124) },
    { MP_ROM_QSTR(MP_QSTR_DP3246),   MP_ROM_INT(DRV_DP3246) },

    // Scan
    { MP_ROM_QSTR(MP_QSTR_MOD8),  MP_ROM_INT(SCAN_MOD8) },
    { MP_ROM_QSTR(MP_QSTR_MOD16), MP_ROM_INT(SCAN_MOD16) },

    // Rotation
    { MP_ROM_QSTR(MP_QSTR_ROTATE_0),   MP_ROM_INT(ROT_0) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_90),  MP_ROM_INT(ROT_90) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_180), MP_ROM_INT(ROT_180) },
    { MP_ROM_QSTR(MP_QSTR_ROTATE_270), MP_ROM_INT(ROT_270) },

    // ClockSpeed
    { MP_ROM_QSTR(MP_QSTR_CLK_8M),  MP_ROM_INT(CLK_8M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_10M), MP_ROM_INT(CLK_10M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_16M), MP_ROM_INT(CLK_16M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_18M), MP_ROM_INT(CLK_18M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_20M), MP_ROM_INT(CLK_20M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_23M), MP_ROM_INT(CLK_23M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_27M), MP_ROM_INT(CLK_27M) },
    { MP_ROM_QSTR(MP_QSTR_CLK_32M), MP_ROM_INT(CLK_32M) },
};

static MP_DEFINE_CONST_DICT(mp_module_hub75_globals, hub75_module_globals_table);

const mp_obj_module_t mp_module_hub75 = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t*)&mp_module_hub75_globals,
};

MP_REGISTER_MODULE(MP_QSTR_hub75, mp_module_hub75);
