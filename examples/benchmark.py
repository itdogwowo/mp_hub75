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


def benchmark_single_fmt(width, height, fmt, frames=100, clock=hub75.CLK_20M):
    """Benchmark a single pixel format within one initialized display session."""
    pf, _ = _FORMAT_MAP[fmt]
    print_heap_caps()
    display = get_display(
        width=width,
        height=height,
        pixel_format=pf,
        clock=clock,
        safe_mode=True,
        brightness=32,
    )
    print_display_info(display, "benchmark")
    buf = _make_buf(width, height, fmt)
    _, bpp = _FORMAT_MAP[fmt]
    buf_size = width * height * bpp

    print(f"\n--- {fmt} ({width}x{height}, {clock // 1000000}MHz, {bpp} bytes/px) ---")
    print(f"Buffer size: {buf_size} bytes")
    print("Session mode: one init per boot, reuse the same display object")
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


def run(width=64, height=32, fmt="RGB888", frames=60, clock=hub75.CLK_8M):
    """Run one benchmark session. Change format only after reset."""
    print("=" * 50)
    print(f"HUB75 Benchmark — {width}x{height}, {fmt}, {frames} frames")
    print("=" * 50)
    fps, size = benchmark_single_fmt(width, height, fmt, frames, clock)
    mbps = fps * size / 1024 / 1024
    print("\n" + "=" * 50)
    print("Summary:")
    print(f"Format : {fmt}")
    print(f"Buffer : {size} B")
    print(f"FPS    : {fps:.1f}")
    print(f"MB/s   : {mbps:.2f}")
    return {fmt: (fps, size)}


# Run directly as benchmark script
if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        print("\nInterrupted.")
