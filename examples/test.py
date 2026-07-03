import gc
import time

import hub75


DEFAULT_PINS = {
    "r1": 25, "g1": 26, "b1": 27,
    "r2": 14, "g2": 12, "b2": 13,
    "a": 23, "b": 19, "c": 5, "d": 17, "e": -1,
    "lat": 4, "oe": 15, "clk": 16,
}


def _make_display(width, height, brightness=16, clock=hub75.CLK_10M, safe_mode=True):
    scan = hub75.MOD16 if height > 32 else hub75.MOD8
    return hub75.Display(
        width=width,
        height=height,
        r1=DEFAULT_PINS["r1"], g1=DEFAULT_PINS["g1"], b1=DEFAULT_PINS["b1"],
        r2=DEFAULT_PINS["r2"], g2=DEFAULT_PINS["g2"], b2=DEFAULT_PINS["b2"],
        a=DEFAULT_PINS["a"], b=DEFAULT_PINS["b"], c=DEFAULT_PINS["c"],
        d=DEFAULT_PINS["d"], e=DEFAULT_PINS["e"],
        lat=DEFAULT_PINS["lat"], oe=DEFAULT_PINS["oe"], clk=DEFAULT_PINS["clk"],
        pixel_format=hub75.RGB888,
        driver=hub75.GENERIC,
        scan=scan,
        clock_speed=clock,
        brightness=brightness,
        min_refresh_rate=30,
        clk_phase_inverted=False,
        safe_mode=safe_mode,
    )


def _fill_solid(buf, width, height, r, g, b):
    for i in range(width * height):
        idx = i * 3
        buf[idx] = r
        buf[idx + 1] = g
        buf[idx + 2] = b


def safe_start(width=64, height=32):
    """Bring-up with conservative timing and brightness."""
    print("hub75 version:", hub75.version())

    display = _make_display(width, height, brightness=1, clock=hub75.CLK_8M, safe_mode=True)
    buf = bytearray(width * height * 3)
    _fill_solid(buf, width, height, 4, 4, 4)

    try:
        print("status(before begin):", display.status())
        print("buffer_info:", display.buffer_info())
        print("[1/5] begin() ...")
        display.begin()
        print("  ok effective:", display.width, "x", display.height)
        print("  status:", display.status())
        print("[2/5] show() dim frame (RGB=4) ...")
        display.show(buf)
        print("  ok")
        time.sleep_ms(500)

        for level in (2, 4, 8, 16, 32):
            print("[3/5] brightness=%d ..." % level)
            display.set_brightness(level)
            print("  status:", display.status())
            time.sleep_ms(300)

        print("[4/5] show() solid colors at brightness=32 ...")
        _fill_solid(buf, width, height, 16, 0, 0)
        display.show(buf)
        time.sleep_ms(400)
        _fill_solid(buf, width, height, 0, 16, 0)
        display.show(buf)
        time.sleep_ms(400)
        _fill_solid(buf, width, height, 0, 0, 16)
        display.show(buf)
        time.sleep_ms(400)

        print("[5/5] safe_start done")
        return True
    finally:
        try:
            display.set_brightness(0)
        except Exception:
            pass
        try:
            display.close()
        finally:
            del display
            gc.collect()


def _fill_checker(buf, width, height, step=8):
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * 3
            on = ((x // step) + (y // step)) & 1
            buf[idx] = 255 if on else 0
            buf[idx + 1] = 255 if not on else 0
            buf[idx + 2] = 32


def _fill_gradient(buf, width, height):
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * 3
            buf[idx] = (x * 255) // max(width - 1, 1)
            buf[idx + 1] = (y * 255) // max(height - 1, 1)
            buf[idx + 2] = ((x + y) * 255) // max(width + height - 2, 1)


def run(width=64, height=32, delay_ms=700, brightness=32):
    print("hub75 version:", hub75.version())

    display = _make_display(width, height, brightness=brightness, clock=hub75.CLK_10M, safe_mode=True)
    buf = bytearray(width * height * 3)

    try:
        print("status(before begin):", display.status())
        print("buffer_info:", display.buffer_info())
        display.begin()
        print("status(after begin):", display.status())

        _fill_solid(buf, width, height, 16, 0, 0)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_solid(buf, width, height, 0, 16, 0)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_solid(buf, width, height, 0, 0, 16)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_checker(buf, width, height)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_gradient(buf, width, height)
        display.show(buf)
        time.sleep_ms(delay_ms * 2)

        print("test done")
        return True
    finally:
        try:
            display.set_brightness(0)
        except Exception:
            pass
        try:
            display.close()
        finally:
            del display
            gc.collect()


if __name__ == "__main__":
    print("=== safe_start ===")
    safe_start()
    print()
    print("=== run (brightness=32) ===")
    run(brightness=32)
