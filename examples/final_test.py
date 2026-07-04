import hub75
import benchmark
import panel_sweep
from display_session import session_active, session_summary, get_heap_caps_info


FORMAT_TO_PIXEL_FORMAT = {
    "RGB888": hub75.RGB888,
    "RGB565": hub75.RGB565,
    "RGBX8888": hub75.RGBX8888,
}


def _scan_for_height(height):
    return hub75.MOD16 if height > 32 else hub75.MOD8


def _expected_key(width, height, fmt, clock, safe_mode):
    return (width, height, FORMAT_TO_PIXEL_FORMAT[fmt], clock, safe_mode, _scan_for_height(height))


def _expected_key_64x32(fmt, clock, safe_mode):
    return _expected_key(64, 32, fmt, clock, safe_mode)


def _print_preflight_header(title, target_fps, clock, brightness, safe_mode, fmt, expected, current, max_panels=None):
    print("=" * 50)
    print(title)
    print("=" * 50)
    print("Target FPS   :", target_fps)
    if max_panels is not None:
        print("Max panels   :", max_panels)
    print("Pixel format :", fmt)
    print("Clock        :", clock // 1000000, "MHz")
    print("Brightness   :", brightness)
    print("Safe mode    :", safe_mode)
    print("Expected key :", expected)
    print("Current key  :", current)


def _preflight_size(width, height, target_fps, clock, safe_mode, brightness, fmt, title, max_panels=None):
    if fmt not in FORMAT_TO_PIXEL_FORMAT:
        raise ValueError("fmt must be RGB888 / RGB565 / RGBX8888")
    expected = _expected_key(width, height, fmt, clock, safe_mode)
    current = session_summary() if session_active() else None
    _print_preflight_header(title, target_fps, clock, brightness, safe_mode, fmt, expected, current, max_panels=max_panels)

    if current is not None and current != expected:
        print()
        print("Preflight failed:")
        print("A different display session is already active.")
        print("This test needs a clean boot or the same config.")
        print("Do a hard reset, reconnect, then run the direct test again.")
        return False

    print("Preflight ok.")
    return True


def preflight_64x32(target_fps=50, max_panels=4, clock=hub75.CLK_20M,
                   safe_mode=False, brightness=32, fmt="RGB888"):
    return _preflight_size(
        width=64,
        height=32,
        target_fps=target_fps,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
        fmt=fmt,
        title="HUB75 Final 64x32 Test",
        max_panels=max_panels,
    )


def run_64x32_final(target_fps=50, max_panels=4, clock=hub75.CLK_20M,
                    brightness=32, safe_mode=False, auto_reset=False, fmt="RGB888"):
    if not preflight_64x32(
        target_fps=target_fps,
        max_panels=max_panels,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
        fmt=fmt,
    ):
        return False

    print()
    print("Starting final 64x32 sweep...")
    print("This run focuses on panels 1..{}.".format(max_panels))
    return panel_sweep.start_64x32_chain(
        max_panels=max_panels,
        target_fps=target_fps,
        clock=clock,
        brightness=brightness,
        safe_mode=safe_mode,
        auto_reset=auto_reset,
        clear_results=True,
        fmt=fmt,
    )


def run_direct(width=128, height=32, target_fps=50, clock=hub75.CLK_20M,
               brightness=32, safe_mode=False, fmt="RGB565",
               try_frames=120, swap_frames=180, retry_delay_ms=1):
    if not _preflight_size(
        width=width,
        height=height,
        target_fps=target_fps,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
        fmt=fmt,
        title="HUB75 Direct Size Test",
    ):
        return False

    heap_info = get_heap_caps_info()
    dma_theory = panel_sweep._theoretical_panel_capacity(height, heap_info=heap_info)
    bus_theory = panel_sweep._theoretical_bus_capacity(height, target_fps, clock)
    panels = width // 64 if width % 64 == 0 else None

    print()
    print("Starting direct test...")
    print("Size         : {}x{}".format(width, height))
    print("Panels       :", panels if panels is not None else "custom")
    print("Target FPS   :", target_fps)
    print("Theory DMA   :", dma_theory.get("max_panels_theoretical_safe"))
    print("Theory bus   :", bus_theory.get("max_panels_by_bus"))

    try_fps, retries = benchmark.benchmark_try_show(
        width=width,
        height=height,
        fmt=fmt,
        frames=try_frames,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
        retry_delay_ms=retry_delay_ms,
    )
    swap_fps = benchmark.benchmark_swap_only(
        width=width,
        height=height,
        fmt=fmt,
        frames=swap_frames,
        clock=clock,
        safe_mode=safe_mode,
        brightness=brightness,
    )

    refresh_rate = panel_sweep._estimate_refresh_rate(width, height, clock)
    passed = try_fps >= target_fps

    print("-" * 50)
    print("Direct test summary")
    print("-" * 50)
    print("size          :", "{}x{}".format(width, height))
    print("fmt           :", fmt)
    print("clock         :", "{} MHz".format(clock // 1000000))
    print("refresh_rate  :", round(refresh_rate, 5))
    print("try_show_fps  :", round(try_fps, 5))
    print("retries       :", retries)
    print("swap_fps      :", round(swap_fps, 5))
    print("theory_dma    :", dma_theory.get("max_panels_theoretical_safe"))
    print("theory_bus    :", bus_theory.get("max_panels_by_bus"))
    print("passed        :", passed)
    return passed


def run_128x32_direct(target_fps=50, clock=hub75.CLK_20M,
                      brightness=32, safe_mode=False, fmt="RGB565",
                      try_frames=120, swap_frames=180, retry_delay_ms=1):
    return run_direct(
        width=128,
        height=32,
        target_fps=target_fps,
        clock=clock,
        brightness=brightness,
        safe_mode=safe_mode,
        fmt=fmt,
        try_frames=try_frames,
        swap_frames=swap_frames,
        retry_delay_ms=retry_delay_ms,
    )


def run_64x32_targets():
    print("=" * 50)
    print("64x32 Target Guide")
    print("=" * 50)
    print("50 FPS -> strict visible performance target")
    print("30 FPS -> practical mid target")
    print("20 FPS -> relaxed target")
    print()
    print("Run one of:")
    print("  final_test.run_64x32_final(target_fps=50, max_panels=4, fmt='RGB888')")
    print("  final_test.run_64x32_final(target_fps=30, max_panels=4, fmt='RGB565')")
    print("  final_test.run_64x32_final(target_fps=20, max_panels=4, fmt='RGB565')")
    return True


def compare_formats(target_fps=50, max_panels=4):
    print("=" * 50)
    print("64x32 Format Compare")
    print("=" * 50)
    print("Run one at a time after a clean reset:")
    print("  final_test.run_64x32_final(target_fps={}, max_panels={}, fmt='RGB888')".format(target_fps, max_panels))
    print("  final_test.run_64x32_final(target_fps={}, max_panels={}, fmt='RGB565')".format(target_fps, max_panels))
    print("  final_test.run_64x32_final(target_fps={}, max_panels={}, fmt='RGBX8888')".format(target_fps, max_panels))
    return True


if __name__ == "__main__":
    run_64x32_final()
