# mp_hub75 Usage Guide

This document explains the practical workflow for using `mp_hub75` on ESP32-S3.

It focuses on:

- safe bring-up
- session-style reuse
- choosing between `show()`, `try_show()`, and `show_blocking()`
- interpreting benchmark results
- understanding what "how many panels" really means

## 1. What This Module Does

`mp_hub75` exposes a MicroPython module named `hub75`.

It drives HUB75 RGB LED matrices on ESP32-S3 using:

- LCD_CAM
- GDMA
- double buffering in the underlying driver

The driver is optimized for high throughput, but repeated hardware deinit and
re-init after soft reboot is not fully reliable on ESP32-S3. Because of that,
the examples use a single init per boot and then reuse the same display session.

## 2. Hard Rules

### 2.1 Do not use GPIO 19 or 20

On ESP32-S3 with native USB CDC enabled, GPIO `19` and `20` are reserved by USB.

Do not use them for HUB75 signals.

### 2.2 Prefer one init per boot

Recommended workflow:

1. initialize once
2. keep the hardware handle alive
3. recreate Python objects by attaching to the shared handle if needed

This is exactly what `examples/display_session.py` is for.

### 2.3 Understand the three frame submission styles

- `display.show(buf)`
  - Fast submit path
  - Does not wait for visible frame completion
  - Best for raw throughput measurement

- `display.try_show(buf) -> bool`
  - Non-blocking
  - Returns `True` if the new frame is accepted now
  - Returns `False` if the previous frame is still considered pending
  - Best when CPU must stay free and you want retry logic

- `display.show_blocking(buf) -> bool`
  - Conservative helper
  - Waits until submission becomes possible, then waits one estimated frame time
  - Returns `False` on timeout or rejection
  - Not a true hardware EOF/vsync acknowledgement

## 3. Example File Layout

Relevant files in this repo:

- `examples/boot.py`
- `examples/display_session.py`
- `examples/test.py`
- `examples/benchmark.py`
- `examples/panel_sweep.py`
- `examples/final_test.py`

## 4. Copy Files To The Board

Typical layout on the MicroPython board:

- `/boot.py`
- `/display_session.py`
- `/test.py`
- `/benchmark.py`
- `/panel_sweep.py`
- `/final_test.py`

`boot.py` should contain the session bootstrap so the board starts in a known state.

## 5. Default Pins

The example helper uses this mapping:

```python
DEFAULT_PINS = {
    "r1": 4, "g1": 5, "b1": 6,
    "r2": 7, "g2": 15, "b2": 16,
    "a": 18, "b": 8, "c": 3, "d": 42, "e": 9,
    "lat": 40, "oe": 2, "clk": 41,
}
```

If your panel wiring differs, adjust `DEFAULT_PINS` in `examples/display_session.py`.

## 6. Minimal Bring-Up

At REPL:

```python
import test
test.safe_start()
```

This uses:

- low brightness
- safe mode
- diagnostics printout

If that works, try:

```python
test.run()
```

## 7. Session Workflow

The recommended runtime entry point is:

```python
from display_session import get_display

display = get_display(
    width=64,
    height=32,
    pixel_format=hub75.RGB888,
    clock=hub75.CLK_8M,
    safe_mode=True,
    brightness=32,
)
```

This gives you:

- one hardware init per boot
- display reuse across script runs in the same session
- brightness control without tearing down the peripheral every time

## 8. Drawing Frames

### 8.1 Using `show()`

```python
buf = bytearray(64 * 32 * 3)
display.show(buf)
```

Use this when:

- you want maximum submit throughput
- you are benchmarking swap speed
- you do not need pacing feedback

### 8.2 Using `try_show()`

```python
buf = bytearray(64 * 32 * 3)

if display.try_show(buf):
    print("accepted")
else:
    print("busy, retry later")
```

Use this when:

- CPU must stay responsive
- you want to know whether the frame was accepted
- you prefer retry logic outside the driver

Typical pattern:

```python
while not display.try_show(buf):
    # do other work here
    pass
```

### 8.3 Using `show_blocking()`

```python
ok = display.show_blocking(buf)
print(ok)
```

Use this when:

- you want a simple pacing helper
- you do not mind waiting briefly

Do not treat it as a true DMA frame-done event. It is based on estimated frame timing.

## 9. Diagnostics

The display object provides:

```python
display.status()
display.buffer_info()
```

`status()` reports fields such as:

- width
- height
- refresh_rate
- brightness
- clock_speed
- bit_depth
- safe_mode
- last_stage
- last_error

`buffer_info()` reports:

- DMA bytes per buffer
- double-buffer payload size
- estimated overhead
- total estimated DMA allocation

## 10. Running Benchmark

### 10.1 Direct run

If you run `benchmark.py` directly, it performs the complete `64x32` test flow:

1. non-blocking `try_show()` pacing test
2. full-speed `show()` swap test
3. visible FPS dwell-time test

### 10.2 From REPL

```python
import benchmark
benchmark.test_64x32()
benchmark.test_64x64()
```

## 11. Progressive Sweep

When you want to find the maximum practical chain length while keeping at least
some target FPS, use `panel_sweep.py`.

This script is designed around the current ESP32-S3 limitation:

- changing panel size in the same boot session is less reliable than hard reset
- so each sweep step tests one size, saves the result, then continues after `machine.reset()`

### 11.1 Start a 64x32 chain sweep

```python
import panel_sweep
panel_sweep.start_64x32_chain(max_panels=8, target_fps=50)
```

Default is `auto_reset=False`.

Reason:

- on ESP32-S3 native USB CDC, `machine.reset()` usually disconnects Thonny
- the sweep therefore saves state first, then waits for you to reset when convenient

This tests:

- `64x32`
- `128x32`
- `192x32`
- and so on, until one step falls below the target or reaches `max_panels`

### 11.2 Start a 64x64 chain sweep

```python
import panel_sweep
panel_sweep.start_64x64_chain(max_panels=4, target_fps=50)
```

This tests:

- `64x64`
- `128x64`
- `192x64`
- and so on

### 11.3 What each step measures

Each step records:

- estimated total DMA requirement
- estimated DMA bytes per buffer
- current largest free DMA block
- theoretical max panels by total DMA
- theoretical max panels by largest DMA block
- theoretical safe panel count as the minimum of those limits
- theoretical max panels by bus refresh at your target FPS
- `TryShow FPS`
- retry count
- `Swap FPS`
- estimated `refresh_rate`

### 11.4 Resume behavior

`examples/boot.py` now tries to resume an active sweep automatically.

So after a step passes, the board can:

1. save the result
2. wait for a manual reset by default
3. continue the next size after reboot

If you really want automatic reset, you can still enable it:

```python
panel_sweep.start_64x32_chain(max_panels=8, target_fps=50, auto_reset=True)
```

### 11.5 Reading the saved results

```python
import panel_sweep
panel_sweep.show_results()
```

The summary now reports three separate limits:

- `Theoretical by DMA`
  - whether the buffers are likely to fit at all
- `Theoretical by bus`
  - whether panel refresh is still theoretically high enough for the target FPS
- `Measured >= target FPS`
  - the largest step that still keeps your target `TryShow FPS`
- `Practical supported`
  - the final usable count, effectively the smaller of the DMA limit, the bus limit, and the measured limit

If the sweep is not finished yet, the summary will show:

- `Measured so far`
- `Current practical`

That means the answer is still provisional until you continue the next steps.

### 11.6 Abort the sweep

```python
import panel_sweep
panel_sweep.abort()
```

### 11.7 Recommended final real test

If you already know the likely range is around `3` to `4` panels, use the
final wrapper:

```python
import final_test
final_test.run_64x32_final(target_fps=50, max_panels=4)
```

This wrapper:

- locks the test to `64x32`
- limits the sweep to `1..4` panels
- checks for an incompatible old session before starting

You can choose the input buffer format with `fmt=`:

```python
import final_test
final_test.run_64x32_final(target_fps=50, max_panels=4, fmt="RGB888")
final_test.run_64x32_final(target_fps=50, max_panels=4, fmt="RGB565")
final_test.run_64x32_final(target_fps=50, max_panels=4, fmt="RGBX8888")
```

## 12. How To Read The Results

### 12.1 `TryShow FPS`

This is the accepted frame rate when using non-blocking pacing.

It is the closest current metric to:

"How often can I submit a frame without overrunning the visible scan timing?"

### 12.2 `Retries`

This counts how often `try_show()` said "not now".

High retry counts usually mean:

- your polling interval is aggressive
- the panel refresh is the real bottleneck

### 12.3 `Swap FPS`

This is raw software-side throughput for repeated `show()` calls.

It can be much higher than real visible FPS because `show()` does not wait for a full panel refresh.

### 12.4 `refresh_rate`

This is the estimated full-panel refresh rate in Hz.

For visible animation, this is the key upper bound. If your panel refreshes at
about `76 Hz`, submitting `300 FPS` does not mean the viewer sees `300 FPS`.

## 13. Does This Tell Me How Many Panels I Can Drive?

Not automatically.

The benchmark tells you the size you are currently testing, not the maximum safe chain length for all configurations.

### 13.1 What it does tell you

For a given `width` and `height`, it tells you:

- the total LED count
- an equivalent panel count for common `64x32` or `64x64` panel sizes
- the refresh rate and submit behavior for that total canvas

Examples:

- `64x32` -> 1 x `64x32`
- `128x32` -> 2 x `64x32`
- `192x32` -> 3 x `64x32`
- `64x64` -> 1 x `64x64`
- `128x64` -> 2 x `64x64` or 4 x `64x32` equivalent area

### 13.2 What it does not tell you

It does not currently:

- scan every possible chain length unless you explicitly run `panel_sweep.py`
- auto-detect the maximum stable number of panels unless you combine `panel_sweep.py` with your target FPS threshold
- verify true frame completion through a hardware EOF callback

### 13.3 Practical meaning

If you want to know "how many panels can I run", test with the actual full canvas.

For example:

- 2 chained `64x32` panels -> test `width=128, height=32`
- 3 chained `64x32` panels -> test `width=192, height=32`
- 4 chained `64x32` panels -> test `width=256, height=32`
- 2 chained `64x64` panels -> test `width=128, height=64`

Then compare:

- theoretical DMA capacity
- theoretical bus capacity at target FPS
- `refresh_rate`
- `TryShow FPS`
- visible stability
- brightness artifacts

As the panel area grows, visible refresh usually drops.
Even if DMA theory says a larger chain fits, your practical limit may still be lower because FPS falls under the target.

## 14. Common Recommendations

### 14.1 First bring-up

Start with:

- `CLK_8M`
- `safe_mode=True`
- brightness `1` to `32`

### 14.2 Throughput testing

Then try:

- `CLK_20M`
- `safe_mode=False`

### 14.3 Stable animation

For animation that must not outrun the panel, prefer:

- `try_show()`
- or your own pacing based on `display.refresh_rate`

## 15. PSRAM And DMA Limits

- The current driver allocates DMA refresh buffers from internal SRAM
- PSRAM does not remove the core DMA bottleneck for HUB75 refresh
- Large PSRAM capacity may help application data, but not the actual DMA scan buffers
- That is why the sweep records `largest free DMA`, not only total SPIRAM

## 16. Known Limits

- No true frame-done callback is exposed yet
- `show_blocking()` is estimated pacing, not hard synchronization
- Wider or taller panel chains reduce refresh rate
- Repeated hard re-init in the same boot session is less reliable than session reuse

## 17. Related Files

- Overview: [README.md](file:///Users/user/Documents/code/git/mp_hub75/README.md)
- Design notes: [DESIGN.md](file:///Users/user/Documents/code/git/mp_hub75/DESIGN.md)
- Session helper: [display_session.py](file:///Users/user/Documents/code/git/mp_hub75/examples/display_session.py)
- Bring-up test: [test.py](file:///Users/user/Documents/code/git/mp_hub75/examples/test.py)
- Benchmark: [benchmark.py](file:///Users/user/Documents/code/git/mp_hub75/examples/benchmark.py)
- Progressive sweep: [panel_sweep.py](file:///Users/user/Documents/code/git/mp_hub75/examples/panel_sweep.py)
