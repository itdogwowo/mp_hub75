import gc

import hub75


DEFAULT_PINS = {
    "r1": 4, "g1": 5, "b1": 6,
    "r2": 7, "g2": 15, "b2": 16,
    "a": 18, "b": 8, "c": 3, "d": 42, "e": 9,
    "lat": 40, "oe": 2, "clk": 41,
}

_STATE = {
    "display": None,
    "config": None,
}


def _scan_for_height(height):
    return hub75.MOD16 if height > 32 else hub75.MOD8


def _session_key(width, height, pixel_format, clock, safe_mode):
    return (width, height, pixel_format, clock, safe_mode, _scan_for_height(height))


def _create_display(width, height, pixel_format, clock, safe_mode, brightness):
    return hub75.Display(
        width=width,
        height=height,
        r1=DEFAULT_PINS["r1"], g1=DEFAULT_PINS["g1"], b1=DEFAULT_PINS["b1"],
        r2=DEFAULT_PINS["r2"], g2=DEFAULT_PINS["g2"], b2=DEFAULT_PINS["b2"],
        a=DEFAULT_PINS["a"], b=DEFAULT_PINS["b"], c=DEFAULT_PINS["c"],
        d=DEFAULT_PINS["d"], e=DEFAULT_PINS["e"],
        lat=DEFAULT_PINS["lat"], oe=DEFAULT_PINS["oe"], clk=DEFAULT_PINS["clk"],
        pixel_format=pixel_format,
        driver=hub75.GENERIC,
        scan=_scan_for_height(height),
        clock_speed=clock,
        brightness=brightness,
        min_refresh_rate=30,
        clk_phase_inverted=False,
        safe_mode=safe_mode,
    )


def get_display(width=64, height=32, pixel_format=hub75.RGB888,
                clock=hub75.CLK_8M, safe_mode=True, brightness=32):
    key = _session_key(width, height, pixel_format, clock, safe_mode)
    display = _STATE["display"]

    if display is None:
        display = _create_display(width, height, pixel_format, clock, safe_mode, brightness)
        display.begin()
        _STATE["display"] = display
        _STATE["config"] = key
    elif _STATE["config"] != key:
        raise RuntimeError(
            "display session already initialized with a different config; "
            "reuse the same config or reset the board"
        )

    display.set_brightness(brightness)
    return display


def standby_display(level=0):
    display = _STATE["display"]
    if display is not None:
        display.set_brightness(level)


def session_active():
    return _STATE["display"] is not None


def session_summary():
    return _STATE["config"]


def print_heap_caps():
    try:
        import heap_caps
    except Exception as exc:
        print("heap_caps: unavailable:", exc)
        return False

    print("heap_caps:")
    for name, caps in (
        ("DMA", heap_caps.CAP_DMA),
        ("INTERNAL", heap_caps.CAP_INTERNAL),
        ("SPIRAM", heap_caps.CAP_SPIRAM),
    ):
        try:
            free = heap_caps.get_free_size(caps)
            total = heap_caps.get_total_size(caps)
            largest = heap_caps.get_largest_free_block(caps)
            print("  {}: free={} total={} largest={}".format(name, free, total, largest))
        except Exception as exc:
            print("  {}: error={}".format(name, exc))
    return True


def print_display_info(display, label="display"):
    try:
        status = display.status()
    except Exception as exc:
        print("{} status: error={}".format(label, exc))
        status = None
    else:
        print("{} status:".format(label), status)

    try:
        info = display.buffer_info()
    except Exception as exc:
        print("{} buffer_info: error={}".format(label, exc))
        info = None
    else:
        print("{} buffer_info:".format(label), info)

    return status, info


def boot_prepare():
    gc.collect()
    return True
