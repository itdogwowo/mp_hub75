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

# Default pin mapping (adjust for your board)
_DEFAULT_PINS = {
    "r1": 25, "g1": 26, "b1": 27,
    "r2": 14, "g2": 12, "b2": 13,
    "a": 23, "b": 19, "c": 5, "d": 17, "e": -1,
    "lat": 4, "oe": 15, "clk": 16,
}

_FORMAT_MAP = {
    "RGB888": (hub75.RGB888, 3),
    "RGB565": (hub75.RGB565, 2),
    "RGBX8888": (hub75.RGBX8888, 4),
}


def _make_display(width=64, height=64, fmt="RGB888", clock=hub75.CLK_20M):
    pf, _ = _FORMAT_MAP[fmt]
    scan = hub75.MOD16 if height > 32 else hub75.MOD8
    return hub75.Display(
        width=width, height=height,
        r1=_DEFAULT_PINS["r1"], g1=_DEFAULT_PINS["g1"], b1=_DEFAULT_PINS["b1"],
        r2=_DEFAULT_PINS["r2"], g2=_DEFAULT_PINS["g2"], b2=_DEFAULT_PINS["b2"],
        a=_DEFAULT_PINS["a"], b=_DEFAULT_PINS["b"], c=_DEFAULT_PINS["c"],
        d=_DEFAULT_PINS["d"], e=_DEFAULT_PINS["e"],
        lat=_DEFAULT_PINS["lat"], oe=_DEFAULT_PINS["oe"], clk=_DEFAULT_PINS["clk"],
        pixel_format=pf,
        scan=scan,
        clock_speed=clock,
    )


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
    """Benchmark a single pixel format."""
    display = _make_display(width, height, fmt, clock)
    buf = _make_buf(width, height, fmt)
    _, bpp = _FORMAT_MAP[fmt]
    buf_size = width * height * bpp

    print(f"\n--- {fmt} ({width}x{height}, {clock // 1000000}MHz, {bpp} bytes/px) ---")
    print(f"Buffer size: {buf_size} bytes")
    print(f"Approx refresh rate: display.refresh_rate (available after begin())")

    info = display.buffer_info()
    print(f"DMA memory: per_buffer={info['dma_per_buffer']}, total={info['total']}")

    display.begin()
    print(f"Refresh rate: {display.refresh_rate:.1f} Hz")
    print(f"Effective dimensions: {display.width}x{display.height}")

    # Warmup
    _fill_pattern(buf, width, height, fmt, 0)
    display.show(buf)

    # Benchmark
    gc.collect()
    t0 = time.ticks_us()
    for f in range(frames):
        _fill_pattern(buf, width, height, fmt, f)
        display.show(buf)
    elapsed = time.ticks_diff(time.ticks_us(), t0) / 1000000.0

    fps = frames / elapsed if elapsed > 0 else 0
    print(f"FPS: {fps:.1f} ({frames} frames in {elapsed:.3f}s, {elapsed/frames*1000:.1f} ms/frame)")

    display.set_brightness(0)  # Minimize power during teardown
    # display.end() is implicit via __del__

    return fps, buf_size


def run(width=64, height=64, frames=60, clock=hub75.CLK_20M):
    """Run benchmarks for all formats."""
    print("=" * 50)
    print(f"HUB75 Benchmark — {width}x{height}, {frames} frames")
    print("=" * 50)

    results = {}
    for fmt in ["RGB888", "RGB565", "RGBX8888"]:
        try:
            fps, size = benchmark_single_fmt(width, height, fmt, frames, clock)
            results[fmt] = (fps, size)
        except MemoryError as e:
            print(f"  SKIPPED: {e}")
        except Exception as e:
            print(f"  ERROR: {e}")

    print("\n" + "=" * 50)
    print("Summary:")
    print(f"{'Format':<12} {'Buf Size':>10} {'FPS':>8} {'MB/s':>8}")
    print("-" * 42)
    for fmt, (fps, size) in results.items():
        mbps = fps * size / 1024 / 1024
        print(f"{fmt:<12} {size:>8} B {fps:>7.1f} {mbps:>7.2f}")

    return results


# Run directly as benchmark script
if __name__ == "__main__":
    try:
        run()
    except KeyboardInterrupt:
        print("\nInterrupted.")
