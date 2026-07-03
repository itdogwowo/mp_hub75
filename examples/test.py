import time
import machine

import hub75
from display_session import get_display, standby_display, print_heap_caps, print_display_info


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
    print_heap_caps()

    display = get_display(
        width=width,
        height=height,
        pixel_format=hub75.RGB888,
        clock=hub75.CLK_8M,
        safe_mode=True,
        brightness=1,
    )
    print_display_info(display, "safe_start")
    buf = bytearray(width * height * 3)
    _fill_solid(buf, width, height, 4, 4, 4)

    print("M1")
    time.sleep_ms(100)
    print("M2")
    time.sleep_ms(100)
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
    standby_display(0)
    return True


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
    print_heap_caps()

    display = get_display(
        width=width,
        height=height,
        pixel_format=hub75.RGB888,
        clock=hub75.CLK_8M,
        safe_mode=True,
        brightness=brightness,
    )
    print_display_info(display, "run")
    buf = bytearray(width * height * 3)

    print("R1")
    time.sleep_ms(100)
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
    standby_display(0)
    return True


if __name__ == "__main__":
    print("=== safe_start ===")
    safe_start()
    print()
    print("=== run (brightness=32) ===")
    run(brightness=32)
