// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: MIT
//
// MicroPython C module for HUB75 RGB LED matrix display
// Wraps esphome/esp-hub75 via C++ bridge layer

#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "src/hub75_bridge.h"

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

// ── Display.make_new ────────────────────────────────────────

enum {
    ARG_width, ARG_height, ARG_r1, ARG_g1, ARG_b1,
    ARG_r2, ARG_g2, ARG_b2,
    ARG_a, ARG_b, ARG_c, ARG_d, ARG_e,
    ARG_lat, ARG_oe, ARG_clk,
    ARG_pixel_format, ARG_driver, ARG_scan,
    ARG_brightness, ARG_rotation, ARG_clock_speed,
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
};

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

    // Optional
    cfg.pixel_format  = parsed[ARG_pixel_format].u_int;
    cfg.shift_driver  = parsed[ARG_driver].u_int;
    cfg.scan_wiring   = parsed[ARG_scan].u_int;
    cfg.brightness    = (uint8_t)parsed[ARG_brightness].u_int;
    cfg.rotation      = parsed[ARG_rotation].u_int;
    cfg.clock_speed   = (uint32_t)parsed[ARG_clock_speed].u_int;
    cfg.clk_phase_inverted = false;
    cfg.min_refresh_rate = 60;

    self->pixel_format = cfg.pixel_format;
    self->width  = w;
    self->height = h;

    self->handle = hub75_bridge_create(&cfg);
    if (!self->handle) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Failed to create HUB75 driver"));
    }

    return MP_OBJ_FROM_PTR(self);
}

// ── Display.begin ───────────────────────────────────────────

static mp_obj_t display_begin(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->running) return mp_const_true;
    if (!hub75_bridge_begin(self->handle)) {
        Hub75BridgeConfig cfg = {0};
        cfg.panel_width  = self->width;
        cfg.panel_height = self->height;
        size_t needed = hub75_bridge_dma_total(&cfg);
        mp_raise_msg_varg(&mp_type_MemoryError,
            MP_ERROR_TEXT("DMA buffer allocation failed: ~%u bytes required. "
                          "Try smaller panel or reduce bit_depth at build time."),
            (unsigned int)needed);
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

    hub75_bridge_show(self->handle, (const uint8_t*)bufinfo.buf, bufinfo.len);
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
        }
    }
}

// ── buffer_info() ───────────────────────────────────────────

static mp_obj_t display_buffer_info(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    Hub75BridgeConfig cfg = {0};
    cfg.panel_width = self->width;
    cfg.panel_height = self->height;
    size_t total = hub75_bridge_dma_total(&cfg);
    size_t per   = total / 2;
    size_t ovh   = total - per * 2;

    mp_obj_t dict = mp_obj_new_dict(4, 0);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&mp_type_dict), dict,
        MP_ROM_QSTR(MP_QSTR_dma_per_buffer), mp_obj_new_int_from_ull(per));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&mp_type_dict), dict,
        MP_ROM_QSTR(MP_QSTR_dma_total), mp_obj_new_int_from_ull(per * 2));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&mp_type_dict), dict,
        MP_ROM_QSTR(MP_QSTR_overhead), mp_obj_new_int_from_ull(ovh));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&mp_type_dict), dict,
        MP_ROM_QSTR(MP_QSTR_total), mp_obj_new_int_from_ull(total));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_buffer_info_obj, display_buffer_info);

// ── __del__ ─────────────────────────────────────────────────

static mp_obj_t display_del(mp_obj_t self_in) {
    hub75_display_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->handle) {
        hub75_bridge_destroy(self->handle);
        self->handle = NULL;
    }
    self->running = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(display_del_obj, display_del);

// ── Display locals_dict ─────────────────────────────────────

static const mp_rom_map_elem_t display_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_begin),          MP_ROM_PTR(&display_begin_obj) },
    { MP_ROM_QSTR(MP_QSTR_show),           MP_ROM_PTR(&display_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_brightness), MP_ROM_PTR(&display_set_brightness_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_rotation),   MP_ROM_PTR(&display_set_rotation_obj) },
    { MP_ROM_QSTR(MP_QSTR_buffer_info),    MP_ROM_PTR(&display_buffer_info_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__),        MP_ROM_PTR(&display_del_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__),      MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__),       MP_ROM_PTR(&display_del_obj) },
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
