import gc
import time
import machine

import hub75


DEFAULT_PINS = {
    # Matches the ESP32-S3-RGB-Matrix reference board examples.
    "r1": 4, "g1": 5, "b1": 6,
    "r2": 7, "g2": 15, "b2": 16,
    "a": 18, "b": 8, "c": 3, "d": 42, "e": 9,
    "lat": 40, "oe": 2, "clk": 41,
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
    print("reset_cause:", machine.reset_cause())
    print("hub75 version:", hub75.version())

    display = _make_display(width, height, brightness=1, clock=hub75.CLK_8M, safe_mode=True)
    buf = bytearray(width * height * 3)
    _fill_solid(buf, width, height, 4, 4, 4)

    try:
        print("M1")
        time.sleep_ms(100)
        print("M2")
        time.sleep_ms(100)
        display.begin()
        print("M3")
        time.sleep_ms(100)
        print("M4")
        time.sleep_ms(100)
        display.show(buf)
        print("M5")
        time.sleep_ms(500)

        for level in (2, 4, 8, 16, 32):
            print("MB", level)
            time.sleep_ms(100)
            display.set_brightness(level)
            print("MX", level)
            time.sleep_ms(300)

        print("M6")
        time.sleep_ms(100)
        _fill_solid(buf, width, height, 16, 0, 0)
        display.show(buf)
        time.sleep_ms(400)
        _fill_solid(buf, width, height, 0, 16, 0)
        display.show(buf)
        time.sleep_ms(400)
        _fill_solid(buf, width, height, 0, 0, 16)
        display.show(buf)
        time.sleep_ms(400)

        print("M7")
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
    print("reset_cause:", machine.reset_cause())
    print("hub75 version:", hub75.version())

    display = _make_display(width, height, brightness=brightness, clock=hub75.CLK_10M, safe_mode=True)
    buf = bytearray(width * height * 3)

    try:
        print("R1")
        time.sleep_ms(100)
        display.begin()
        print("R2")
        time.sleep_ms(100)

        _fill_solid(buf, width, height, 16, 0, 0)
        print("R3")
        time.sleep_ms(100)
        display.show(buf)
        print("R4")
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
