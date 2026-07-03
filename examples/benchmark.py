# HUB75 Benchmark — tests show() throughput with various pixel formats
#
# Usage:
#   import benchmark
#   benchmark.run(width=64, height=64, fmt="RGB888", frames=100)
#
# Or run directly for interactive test.

import hub75
import time
import gc
from display_session import get_display, standby_display, print_heap_caps, print_display_info

_FORMAT_MAP = {
    "RGB888": (hub75.RGB888, 3),
    "RGB565": (hub75.RGB565, 2),
    "RGBX8888": (hub75.RGBX8888, 4),
}


def _make_buf(width, height, fmt):
    _, bpp = _FORMAT_MAP[fmt]
    return bytearray(width * height * bpp)


def _fill_pattern(buf, width, height, fmt, frame):
    """Fill buffer with a test pattern that changes each frame."""
    _, bpp = _FORMAT_MAP[fmt]
    for y in range(height):
        for x in range(width):
            r = (x + frame * 7) & 0xFF
            g = (y + frame * 13) & 0xFF
            b = ((x ^ y) + frame * 17) & 0xFF
            idx = (y * width + x) * bpp
            if fmt == "RGB888":
                buf[idx] = r
                buf[idx + 1] = g
                buf[idx + 2] = b
            elif fmt == "RGB565":
                val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                buf[idx] = val & 0xFF
                buf[idx + 1] = (val >> 8) & 0xFF
            elif fmt == "RGBX8888":
                buf[idx] = r
                buf[idx + 1] = g
                buf[idx + 2] = b
                buf[idx + 3] = 0


def _fill_solid(buf, width, height, fmt, r, g, b):
    _, bpp = _FORMAT_MAP[fmt]
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * bpp
            if fmt == "RGB888":
                buf[idx] = r
                buf[idx + 1] = g
                buf[idx + 2] = b
            elif fmt == "RGB565":
                val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                buf[idx] = val & 0xFF
                buf[idx + 1] = (val >> 8) & 0xFF
            elif fmt == "RGBX8888":
                buf[idx] = r
                buf[idx + 1] = g
                buf[idx + 2] = b
                buf[idx + 3] = 0


def _print_topology(width, height):
    leds = width * height
    print(f"LEDs: {leds} total")
    if width % 64 == 0 and height % 32 == 0:
        panels = (width // 64) * (height // 32)
        print(f"Equivalent 64x32 panels: {panels}")
    elif width % 64 == 0 and height % 64 == 0:
        panels = (width // 64) * (height // 64)
        print(f"Equivalent 64x64 panels: {panels}")
    print("Note: same HUB75 bus can chain more panels, but wider chains usually lower refresh rate")


def benchmark_single_fmt(width, height, fmt, frames=100, clock=hub75.CLK_20M, safe_mode=True):
    """Benchmark a single pixel format within one initialized display session."""
    pf, _ = _FORMAT_MAP[fmt]
    print_heap_caps()
    display = get_display(
        width=width,
        height=height,
        pixel_format=pf,
        clock=clock,
        safe_mode=safe_mode,
        brightness=32,
    )
    print_display_info(display, "benchmark")
    buf = _make_buf(width, height, fmt)
    _, bpp = _FORMAT_MAP[fmt]
    buf_size = width * height * bpp

    print(f"\n--- {fmt} ({width}x{height}, {clock // 1000000}MHz, {bpp} bytes/px) ---")
    print(f"Buffer size: {buf_size} bytes")
    print("Session mode: one init per boot, reuse the same display object")
    _print_topology(width, height)
    info = display.buffer_info()
    per_buffer, _, _, total = info
    print(f"DMA memory: per_buffer={per_buffer}, total={total}")
    print(f"Refresh rate: {display.refresh_rate:.1f} Hz")
    print(f"Effective dimensions: {display.width}x{display.height}")

    _fill_pattern(buf, width, height, fmt, 0)
    display.show(buf)

    gc.collect()
    t0 = time.ticks_us()
    for f in range(frames):
        _fill_pattern(buf, width, height, fmt, f)
        display.show(buf)
    elapsed = time.ticks_diff(time.ticks_us(), t0) / 1000000.0

    fps = frames / elapsed if elapsed > 0 else 0
    print(f"FPS: {fps:.1f} ({frames} frames in {elapsed:.3f}s, {elapsed/frames*1000:.1f} ms/frame)")
    standby_display(0)
    return fps, buf_size


def benchmark_swap_only(width=64, height=32, fmt="RGB888", frames=300,
                        clock=hub75.CLK_20M, safe_mode=False, brightness=32,
                        color_a=(100, 0, 0), color_b=(0, 0, 100)):
    """Benchmark show()+flip path using two full-frame colored buffers."""
    pf, _ = _FORMAT_MAP[fmt]
    print_heap_caps()
    display = get_display(
        width=width,
        height=height,
        pixel_format=pf,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
    )
    print_display_info(display, "swap")

    frame_a = _make_buf(width, height, fmt)
    frame_b = _make_buf(width, height, fmt)
    _fill_solid(frame_a, width, height, fmt, *color_a)
    _fill_solid(frame_b, width, height, fmt, *color_b)

    print(f"\n--- swap-only {fmt} ({width}x{height}, {clock // 1000000}MHz, safe_mode={safe_mode}) ---")
    print("Mode: alternate two prebuilt full-color frames, no per-frame pixel generation")
    print(f"Color A: {color_a}")
    print(f"Color B: {color_b}")
    _print_topology(width, height)
    display.show(frame_a)
    display.show(frame_b)

    gc.collect()
    t0 = time.ticks_us()
    for frame in range(frames):
        display.show(frame_b if (frame & 1) else frame_a)
    elapsed = time.ticks_diff(time.ticks_us(), t0) / 1000000.0
    fps = frames / elapsed if elapsed > 0 else 0
    print(f"Swap FPS: {fps:.1f} ({frames} swaps in {elapsed:.3f}s, {elapsed/frames*1000:.1f} ms/swap)")
    standby_display(0)
    return fps


def benchmark_try_show(width=64, height=32, fmt="RGB888", frames=120,
                       clock=hub75.CLK_20M, safe_mode=False, brightness=32,
                       color_a=(40, 0, 0), color_b=(0, 0, 40), retry_delay_ms=1):
    """Benchmark non-blocking submit pacing using try_show()."""
    pf, _ = _FORMAT_MAP[fmt]
    print_heap_caps()
    display = get_display(
        width=width,
        height=height,
        pixel_format=pf,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
    )
    print_display_info(display, "try_show")

    frame_a = _make_buf(width, height, fmt)
    frame_b = _make_buf(width, height, fmt)
    _fill_solid(frame_a, width, height, fmt, *color_a)
    _fill_solid(frame_b, width, height, fmt, *color_b)

    print(f"\n--- try-show {fmt} ({width}x{height}, {clock // 1000000}MHz, safe_mode={safe_mode}) ---")
    print("Mode: non-blocking submit, retry later if previous frame is still pending")
    print(f"Color A: {color_a}")
    print(f"Color B: {color_b}")
    _print_topology(width, height)

    accepted = 0
    retries = 0
    gc.collect()
    t0 = time.ticks_us()
    while accepted < frames:
        buf = frame_b if (accepted & 1) else frame_a
        if display.try_show(buf):
            accepted += 1
        else:
            retries += 1
            time.sleep_ms(retry_delay_ms)
    elapsed = time.ticks_diff(time.ticks_us(), t0) / 1000000.0
    fps = accepted / elapsed if elapsed > 0 else 0
    print(f"Accepted FPS: {fps:.1f} ({accepted} accepted in {elapsed:.3f}s)")
    print(f"Retries     : {retries} ({retry_delay_ms} ms idle between retries)")
    standby_display(0)
    return fps, retries


def visible_fps_test(width=64, height=32, fmt="RGB888", clock=hub75.CLK_20M,
                     safe_mode=False, brightness=32,
                     hold_ms_list=(20, 16, 13, 10),
                     color_a=(20, 0, 0), color_b=(0, 0, 20), flips_per_step=24):
    """Show two full frames with real dwell time to test visually stable FPS."""
    pf, _ = _FORMAT_MAP[fmt]
    print_heap_caps()
    display = get_display(
        width=width,
        height=height,
        pixel_format=pf,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
    )
    print_display_info(display, "visible")

    frame_a = _make_buf(width, height, fmt)
    frame_b = _make_buf(width, height, fmt)
    _fill_solid(frame_a, width, height, fmt, *color_a)
    _fill_solid(frame_b, width, height, fmt, *color_b)

    print(f"\n--- visible-fps {fmt} ({width}x{height}, {clock // 1000000}MHz, safe_mode={safe_mode}) ---")
    print(f"Color A: {color_a}")
    print(f"Color B: {color_b}")
    _print_topology(width, height)
    for hold_ms in hold_ms_list:
        fps = 1000.0 / hold_ms if hold_ms else 0.0
        print(f"hold={hold_ms} ms -> target visible fps={fps:.1f}")
        for flip in range(flips_per_step):
            display.show(frame_a if (flip & 1) else frame_b)
            time.sleep_ms(hold_ms)
    standby_display(0)
    return True


def test_panel(width, height, fmt="RGB888", clock=hub75.CLK_20M, brightness=32):
    """One-stop test for a single panel size. Reset before changing size/clock."""
    print("=" * 50)
    print(f"HUB75 Panel Test — {width}x{height}")
    print("=" * 50)
    print("Step 1: non-blocking try_show test")
    accepted_fps, retries = benchmark_try_show(
        width=width,
        height=height,
        fmt=fmt,
        frames=120,
        clock=clock,
        safe_mode=False,
        brightness=brightness,
    )
    print()
    print("Step 2: full-speed swap test")
    swap_fps = run_full_speed(
        width=width,
        height=height,
        fmt=fmt,
        frames=300,
        clock=clock,
        brightness=brightness,
    )
    print()
    print("Step 3: visible FPS test")
    visible_fps_test(
        width=width,
        height=height,
        fmt=fmt,
        clock=clock,
        safe_mode=False,
        brightness=brightness,
        hold_ms_list=(20, 16, 13, 10),
        color_a=(20, 0, 0),
        color_b=(0, 0, 20),
        flips_per_step=20,
    )
    print()
    print("=" * 50)
    print("Panel Test Summary")
    print(f"Size        : {width}x{height}")
    print(f"Clock       : {clock // 1000000} MHz")
    print(f"Format      : {fmt}")
    print(f"Brightness  : {brightness}")
    print(f"TryShow FPS : {accepted_fps:.1f}")
    print(f"Retries     : {retries}")
    print(f"Swap FPS    : {swap_fps:.1f}")
    print("Visible FPS : observe 20 / 16 / 13 / 10 ms steps on panel")
    print("Important   : reset board before switching to another size or clock")
    return {
        "try_show_fps": accepted_fps,
        "retries": retries,
        "swap_fps": swap_fps,
    }


def test_64x32(clock=hub75.CLK_20M, brightness=32):
    return test_panel(64, 32, fmt="RGB888", clock=clock, brightness=brightness)


def test_64x64(clock=hub75.CLK_20M, brightness=32):
    return test_panel(64, 64, fmt="RGB888", clock=clock, brightness=brightness)


def run(width=64, height=32, fmt="RGB888", frames=60, clock=hub75.CLK_8M, safe_mode=True):
    """Run one benchmark session. Change format only after reset."""
    print("=" * 50)
    print(f"HUB75 Benchmark — {width}x{height}, {fmt}, {frames} frames")
    print("=" * 50)
    fps, size = benchmark_single_fmt(width, height, fmt, frames, clock, safe_mode=safe_mode)
    mbps = fps * size / 1024 / 1024
    print("\n" + "=" * 50)
    print("Summary:")
    print(f"Format : {fmt}")
    print(f"Buffer : {size} B")
    print(f"FPS    : {fps:.1f}")
    print(f"MB/s   : {mbps:.2f}")
    return {fmt: (fps, size)}


def run_full_speed(width=64, height=32, fmt="RGB888", frames=300,
                   clock=hub75.CLK_20M, brightness=32):
    """Try the fastest practical swap test. Reboot before changing clock/safe_mode."""
    print("=" * 50)
    print(f"HUB75 Full-Speed Swap — {width}x{height}, {fmt}, {frames} swaps")
    print("=" * 50)
    fps = benchmark_swap_only(
        width=width,
        height=height,
        fmt=fmt,
        frames=frames,
        clock=clock,
        safe_mode=False,
        brightness=brightness,
    )
    print("\n" + "=" * 50)
    print("Summary:")
    print(f"Format   : {fmt}")
    print(f"Clock    : {clock // 1000000} MHz")
    print("SafeMode : False")
    print(f"Swap FPS : {fps:.1f}")
    return fps


# Run directly as benchmark script
if __name__ == "__main__":
    try:
        test_64x32()
    except KeyboardInterrupt:
        print("\nInterrupted.")
