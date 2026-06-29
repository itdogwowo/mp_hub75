import gc
import time

import hub75


DEFAULT_PINS = {
    "r1": 25, "g1": 26, "b1": 27,
    "r2": 14, "g2": 12, "b2": 13,
    "a": 23, "b": 19, "c": 5, "d": 17, "e": -1,
    "lat": 4, "oe": 15, "clk": 16,
}


def _make_display(width, height, brightness=96, clock=hub75.CLK_20M):
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
    )


def _fill_solid(buf, width, height, r, g, b):
    for i in range(width * height):
        idx = i * 3
        buf[idx] = r
        buf[idx + 1] = g
        buf[idx + 2] = b


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


def run(width=64, height=32, delay_ms=700):
    print("hub75 version:", hub75.version())

    display = _make_display(width, height)
    buf = bytearray(width * height * 3)

    try:
        info = display.buffer_info()
        print("buffer_info:", info)

        display.begin()
        print("effective size:", display.width, "x", display.height)
        print("refresh_rate:", display.refresh_rate)
        print("is_running:", display.is_running)

        _fill_solid(buf, width, height, 255, 0, 0)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_solid(buf, width, height, 0, 255, 0)
        display.show(buf)
        time.sleep_ms(delay_ms)

        _fill_solid(buf, width, height, 0, 0, 255)
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
        if display:
            display.set_brightness(0)
            del display
        gc.collect()


if __name__ == "__main__":
    run()
