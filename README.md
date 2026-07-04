# mp_hub75

High-performance MicroPython C module for ESP32-S3 HUB75 RGB LED matrices.

This project wraps `esphome/esp-hub75` behind a MicroPython `hub75` module and
is tuned for practical bring-up on ESP32-S3 with LCD_CAM + GDMA.

## Status

- Works on ESP32-S3 with HUB75 panels such as `64x32` and `64x64`
- Uses double buffering in the driver
- DMA buffers live in internal SRAM, not PSRAM
- Supports session-style reuse to avoid repeated LCD_CAM re-init issues after soft reboot
- Provides `show()`, `try_show()`, `show_blocking()`, diagnostics, and benchmark examples
- Includes `panel_sweep.py` for progressive multi-panel testing across hard resets

## Important Notes

- Do not use GPIO `19` or `20` for HUB75 signals on ESP32-S3 when native USB CDC is enabled
- Prefer one hardware init per boot, then reuse the same display session
- `show()` is non-blocking and measures software submit throughput
- `try_show()` is non-blocking and returns whether the frame was accepted now
- `show_blocking()` is a conservative helper based on estimated frame time, not a true DMA EOF callback

## Quick Start

1. Build the MicroPython firmware with this user C module.
2. Copy the example files from `examples/` onto the board.
3. Put `examples/boot.py` on the board root as `/boot.py`.
4. Put `examples/display_session.py`, `examples/test.py`, `examples/benchmark.py`, `examples/panel_sweep.py`, and `examples/final_test.py` on the board.
5. Reboot and run the tests from REPL.

Example:

```python
import test
test.safe_start()
test.run()
```

Full benchmark:

```python
import benchmark
benchmark.test_64x32()
```

Progressive panel sweep until `TryShow FPS` falls below `50`:

```python
import panel_sweep
panel_sweep.start_64x32_chain(max_panels=8, target_fps=50)
```

Recommended final real-world test for your current case:

```python
import final_test
final_test.run_64x32_final(target_fps=50, max_panels=4)
```

You can also choose the input buffer format explicitly:

```python
final_test.run_64x32_final(target_fps=50, max_panels=4, fmt="RGB565")
```

The sweep now reports both:

- theoretical panel limit from DMA buffer capacity
- theoretical panel limit from bus refresh at your target FPS
- measured panel limit from `TryShow FPS`

So you can judge practical support as:

- enough DMA to allocate the buffers
- enough bus refresh to still meet target visible FPS
- and still enough FPS to remain useful

Default behavior is `auto_reset=False` because ESP32-S3 native USB CDC reset
will usually disconnect Thonny temporarily. The sweep state is saved, then you
can reset manually and let `/boot.py` resume the next step.

If `benchmark.py` is run directly, it now performs the complete `64x32` panel test:

- non-blocking `try_show()` pacing test
- full-speed `show()` swap test
- visible FPS dwell-time test

If `panel_sweep.py` is started, `boot.py` can resume the next size after
`machine.reset()` until the sweep stops or reaches `max_panels`.

## Does The Benchmark Tell Me How Many Panels I Can Drive?

Partly.

- The benchmark reports the total logical canvas size you are currently testing
- For `64x32`, it prints the equivalent number of `64x32` panels
- For `64x64`, it prints the equivalent number of `64x64` panels
- It does not automatically sweep all possible chain lengths and find your maximum stable panel count
- Use `panel_sweep.py` if you want that progressive test automatically
- `panel_sweep.py` now combines DMA-based theory and FPS-based measurement in one summary

Examples:

- `64x32` means 1 x `64x32` panel
- `128x32` means 2 x `64x32` panels on one logical canvas
- `192x32` means 3 x `64x32` panels on one logical canvas
- `64x64` means 1 x `64x64` panel, or an equivalent `2 x 64x32` area
- `128x64` means an area equivalent to 2 x `64x64` or 4 x `64x32`

As panel area increases, refresh rate drops roughly in proportion. So the real
question is not only "how many panels can it address", but "how many panels can
it address while still keeping acceptable visible refresh".

## DMA And PSRAM

- Current driver DMA buffers are allocated from internal SRAM
- PSRAM does not remove the DMA allocation limit for panel refresh buffers
- PSRAM may still exist on the board and show up in `heap_caps`, but it is not the critical pool for HUB75 DMA
- For this reason, `panel_sweep.py` focuses on actual bring-up plus FPS, not just total free PSRAM

## Default Example Pins

The example session helper currently uses this ESP32-S3-safe mapping:

```python
DEFAULT_PINS = {
    "r1": 4, "g1": 5, "b1": 6,
    "r2": 7, "g2": 15, "b2": 16,
    "a": 18, "b": 8, "c": 3, "d": 42, "e": 9,
    "lat": 40, "oe": 2, "clk": 41,
}
```

See [USAGE.md](file:///Users/user/Documents/code/git/mp_hub75/USAGE.md) for the
full workflow, API guidance, benchmark interpretation, multi-panel notes, and
progressive sweep usage.
