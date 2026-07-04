import gc
import time
import machine

import hub75
import benchmark
from display_session import get_display, get_heap_caps_info

try:
    import ujson as json
except ImportError:
    import json


STATE_FILE = "panel_sweep_state.json"
RESULTS_FILE = "panel_sweep_results.json"
FORMAT_TO_PIXEL_FORMAT = {
    "RGB888": hub75.RGB888,
    "RGB565": hub75.RGB565,
    "RGBX8888": hub75.RGBX8888,
}


def _estimate_dma_total(width, height, bit_depth=8):
    rows = height // 2
    per_buffer = rows * width * bit_depth * 2
    overhead = rows * bit_depth * 12 * 2 + rows * 16 * 2
    return {
        "per_buffer": per_buffer,
        "double_buffer": per_buffer * 2,
        "overhead": overhead,
        "total": per_buffer * 2 + overhead,
    }


def _estimate_refresh_rate(width, height, clock, bit_depth=8):
    rows = height // 2
    if rows <= 0 or width <= 0 or clock <= 0:
        return 0.0
    bcm = (1 << bit_depth) - 1
    denom = rows * bcm * width
    if denom <= 0:
        return 0.0
    return float(clock) / float(denom)


def _theoretical_panel_capacity(height, panel_width=64, heap_info=None, bit_depth=8, max_search=256):
    dma_info = (heap_info or {}).get("DMA", {})
    free_dma = dma_info.get("free")
    largest_dma = dma_info.get("largest")

    by_total = 0
    by_largest = 0
    by_both = 0

    for panels in range(1, max_search + 1):
        est = _estimate_dma_total(panel_width * panels, height, bit_depth=bit_depth)
        fits_total = free_dma is None or est["total"] <= free_dma
        fits_largest = largest_dma is None or est["per_buffer"] <= largest_dma

        if fits_total:
            by_total = panels
        if fits_largest:
            by_largest = panels
        if fits_total and fits_largest:
            by_both = panels

    return {
        "free_dma": free_dma,
        "largest_dma": largest_dma,
        "max_panels_by_total_dma": by_total,
        "max_panels_by_largest_block": by_largest,
        "max_panels_theoretical_safe": by_both,
    }


def _theoretical_bus_capacity(height, target_fps, clock, panel_width=64, bit_depth=8, max_search=256):
    max_panels = 0
    base_refresh = _estimate_refresh_rate(panel_width, height, clock, bit_depth=bit_depth)

    for panels in range(1, max_search + 1):
        refresh = _estimate_refresh_rate(panel_width * panels, height, clock, bit_depth=bit_depth)
        if refresh >= target_fps:
            max_panels = panels
        else:
            break

    return {
        "base_refresh": base_refresh,
        "target_fps": target_fps,
        "max_panels_by_bus": max_panels,
    }


def _load_json(path, default):
    try:
        with open(path, "r") as fp:
            return json.load(fp)
    except Exception:
        return default


def _save_json(path, data):
    with open(path, "w") as fp:
        json.dump(data, fp)


def _remove_file(path):
    try:
        import os
        os.remove(path)
    except Exception:
        pass


def _load_state():
    return _load_json(STATE_FILE, None)


def _save_state(state):
    _save_json(STATE_FILE, state)


def _clear_state():
    _remove_file(STATE_FILE)


def _load_results():
    return _load_json(RESULTS_FILE, [])


def _save_results(results):
    _save_json(RESULTS_FILE, results)


def _append_result(item):
    results = _load_results()
    results.append(item)
    _save_results(results)


def _panel_label(width, height, panel_width):
    if panel_width <= 0:
        return "{}x{}".format(width, height)
    return "{}x{} ({} x {}x{})".format(width, height, width // panel_width, panel_width, height)


def _new_state(height, max_panels, target_fps, clock, brightness,
               safe_mode, try_frames, swap_frames, retry_delay_ms, auto_reset, fmt):
    heap_info = get_heap_caps_info()
    theory = _theoretical_panel_capacity(height, panel_width=64, heap_info=heap_info)
    bus_theory = _theoretical_bus_capacity(
        height,
        target_fps=target_fps,
        clock=clock,
        panel_width=64,
    )
    return {
        "active": True,
        "height": height,
        "panel_width": 64,
        "current_panels": 1,
        "max_panels": max_panels,
        "target_fps": target_fps,
        "fmt": fmt,
        "pixel_format": FORMAT_TO_PIXEL_FORMAT[fmt],
        "clock": clock,
        "brightness": brightness,
        "safe_mode": safe_mode,
        "try_frames": try_frames,
        "swap_frames": swap_frames,
        "retry_delay_ms": retry_delay_ms,
        "auto_reset": auto_reset,
        "initial_heap_dma": theory,
        "initial_bus_theory": bus_theory,
        "started_at_ms": time.ticks_ms(),
    }


def _describe_plan(state):
    print("=" * 50)
    print("HUB75 Progressive Sweep")
    print("=" * 50)
    print("Target panel height :", state["height"])
    print("Panel width         :", state["panel_width"])
    print("Max panels          :", state["max_panels"])
    print("Target TryShow FPS  :", state["target_fps"])
    print("Pixel format        :", state["fmt"])
    print("Clock               :", state["clock"] // 1000000, "MHz")
    print("Brightness          :", state["brightness"])
    print("Safe mode           :", state["safe_mode"])
    print("Auto reset          :", state["auto_reset"])
    theory = state.get("initial_heap_dma", {})
    if theory:
        print("DMA free            :", theory.get("free_dma"))
        print("DMA largest block   :", theory.get("largest_dma"))
        print("Theory by total DMA :", theory.get("max_panels_by_total_dma"))
        print("Theory by block     :", theory.get("max_panels_by_largest_block"))
        print("Theory safe panels  :", theory.get("max_panels_theoretical_safe"))
    bus_theory = state.get("initial_bus_theory", {})
    if bus_theory:
        print("Base refresh 1 panel:", bus_theory.get("base_refresh"))
        print("Theory by bus @ fps :", bus_theory.get("max_panels_by_bus"))


def _result_summary(item):
    print("-" * 50)
    print("panels        :", item.get("panels"))
    print("size          :", "{}x{}".format(item.get("width"), item.get("height")))
    print("refresh_rate  :", item.get("refresh_rate"))
    print("try_show_fps  :", item.get("try_show_fps"))
    print("retries       :", item.get("retries"))
    print("swap_fps      :", item.get("swap_fps"))
    print("dma_total_est :", item.get("dma_total_est"))
    print("dma_per_buf   :", item.get("dma_per_buffer_est"))
    print("dma_largest   :", item.get("dma_largest"))
    print("theory_total  :", item.get("max_panels_by_total_dma"))
    print("theory_block  :", item.get("max_panels_by_largest_block"))
    print("theory_safe   :", item.get("max_panels_theoretical_safe"))
    print("theory_bus    :", item.get("max_panels_by_bus"))
    print("passed        :", item.get("passed"))
    if item.get("error"):
        print("error         :", item.get("error"))


def _measure_current_step(state):
    width = state["panel_width"] * state["current_panels"]
    height = state["height"]
    dma_est = _estimate_dma_total(width, height)
    heap_info = get_heap_caps_info()
    theory = _theoretical_panel_capacity(height, panel_width=state["panel_width"], heap_info=heap_info)
    bus_theory = _theoretical_bus_capacity(
        height,
        target_fps=state["target_fps"],
        clock=state["clock"],
        panel_width=state["panel_width"],
    )
    dma_largest = None
    if heap_info.get("available") and "DMA" in heap_info and "largest" in heap_info["DMA"]:
        dma_largest = heap_info["DMA"]["largest"]

    print()
    print("=" * 50)
    print("Sweep Step {}/{}".format(state["current_panels"], state["max_panels"]))
    print("=" * 50)
    print("Testing:", _panel_label(width, height, state["panel_width"]))
    print("Estimated DMA total:", dma_est["total"])
    print("Estimated DMA/buf  :", dma_est["per_buffer"])
    if dma_largest is not None:
        print("Largest free DMA   :", dma_largest)
    print("Theory by total DMA:", theory.get("max_panels_by_total_dma"))
    print("Theory by block    :", theory.get("max_panels_by_largest_block"))
    print("Theory safe panels :", theory.get("max_panels_theoretical_safe"))
    print("Theory by bus      :", bus_theory.get("max_panels_by_bus"))

    item = {
        "panels": state["current_panels"],
        "width": width,
        "height": height,
        "fmt": state["fmt"],
        "clock": state["clock"],
        "brightness": state["brightness"],
        "safe_mode": state["safe_mode"],
        "target_fps": state["target_fps"],
        "dma_total_est": dma_est["total"],
        "dma_per_buffer_est": dma_est["per_buffer"],
        "dma_largest": dma_largest,
        "max_panels_by_total_dma": theory.get("max_panels_by_total_dma"),
        "max_panels_by_largest_block": theory.get("max_panels_by_largest_block"),
        "max_panels_theoretical_safe": theory.get("max_panels_theoretical_safe"),
        "max_panels_by_bus": bus_theory.get("max_panels_by_bus"),
        "passed": False,
        "error": None,
    }

    if theory.get("max_panels_theoretical_safe", 0) < state["current_panels"]:
        item["error"] = "theoretical DMA limit reached before benchmark"
        item["refresh_rate"] = None
        item["try_show_fps"] = None
        item["retries"] = None
        item["swap_fps"] = None
        item["last_stage"] = None
        item["last_error"] = None
        _append_result(item)
        _result_summary(item)
        return item

    try:
        try_fps, retries = benchmark.benchmark_try_show(
            width=width,
            height=height,
            fmt=state["fmt"],
            frames=state["try_frames"],
            clock=state["clock"],
            safe_mode=state["safe_mode"],
            brightness=state["brightness"],
            retry_delay_ms=state["retry_delay_ms"],
        )
        swap_fps = benchmark.benchmark_swap_only(
            width=width,
            height=height,
            fmt=state["fmt"],
            frames=state["swap_frames"],
            clock=state["clock"],
            safe_mode=state["safe_mode"],
            brightness=state["brightness"],
        )
        gc.collect()
        display = get_display(
            width=width,
            height=height,
            pixel_format=state["pixel_format"],
            clock=state["clock"],
            safe_mode=state["safe_mode"],
            brightness=state["brightness"],
        )
        status = display.status()
        item["refresh_rate"] = status.get("refresh_rate")
        item["try_show_fps"] = try_fps
        item["retries"] = retries
        item["swap_fps"] = swap_fps
        item["last_stage"] = status.get("last_stage")
        item["last_error"] = status.get("last_error")
        item["passed"] = try_fps >= state["target_fps"]
    except Exception as exc:
        item["error"] = str(exc)
        item["refresh_rate"] = None
        item["try_show_fps"] = None
        item["retries"] = None
        item["swap_fps"] = None
        item["last_stage"] = None
        item["last_error"] = None

    _append_result(item)
    _result_summary(item)
    return item


def _finish(state, reason):
    state["active"] = False
    state["finished_reason"] = reason
    _save_state(state)
    print()
    print("=" * 50)
    print("Sweep Finished")
    print("=" * 50)
    print("Reason:", reason)
    show_results()


def _continue_or_finish(state, item):
    if item.get("error"):
        _finish(state, "step failed: {}".format(item["error"]))
        return

    if not item.get("passed"):
        _finish(state, "try_show_fps dropped below target")
        return

    if state["current_panels"] >= state["max_panels"]:
        _finish(state, "reached max_panels")
        return

    state["current_panels"] += 1
    _save_state(state)
    print()
    print("Next step requires hard reset because panel size changes.")
    print("Next test:", "{}x{}".format(state["panel_width"] * state["current_panels"], state["height"]))
    if state["auto_reset"]:
        print("Resetting in 1500 ms...")
        time.sleep_ms(1500)
        machine.reset()
    else:
        print("State saved. On native USB CDC, reset will disconnect Thonny temporarily.")
        print("Reconnect if needed, then let /boot.py resume automatically.")
        print("Call machine.reset() when ready to continue.")


def start(height=32, max_panels=8, target_fps=50.0, clock=hub75.CLK_20M,
          brightness=32, safe_mode=False, try_frames=120, swap_frames=180,
          retry_delay_ms=1, auto_reset=False, clear_results=True, fmt="RGB888"):
    if fmt not in FORMAT_TO_PIXEL_FORMAT:
        raise ValueError("fmt must be RGB888 / RGB565 / RGBX8888")
    if clear_results:
        _remove_file(RESULTS_FILE)
    state = _new_state(
        height=height,
        max_panels=max_panels,
        target_fps=target_fps,
        clock=clock,
        brightness=brightness,
        safe_mode=safe_mode,
        try_frames=try_frames,
        swap_frames=swap_frames,
        retry_delay_ms=retry_delay_ms,
        auto_reset=auto_reset,
        fmt=fmt,
    )
    _save_state(state)
    _describe_plan(state)
    run_pending()


def start_64x32_chain(max_panels=8, target_fps=50.0, clock=hub75.CLK_20M,
                      brightness=32, safe_mode=False, try_frames=120,
                      swap_frames=180, retry_delay_ms=1, auto_reset=False,
                      clear_results=True, fmt="RGB888"):
    start(
        height=32,
        max_panels=max_panels,
        target_fps=target_fps,
        clock=clock,
        brightness=brightness,
        safe_mode=safe_mode,
        try_frames=try_frames,
        swap_frames=swap_frames,
        retry_delay_ms=retry_delay_ms,
        auto_reset=auto_reset,
        clear_results=clear_results,
        fmt=fmt,
    )


def start_64x64_chain(max_panels=4, target_fps=50.0, clock=hub75.CLK_20M,
                      brightness=32, safe_mode=False, try_frames=120,
                      swap_frames=180, retry_delay_ms=1, auto_reset=False,
                      clear_results=True, fmt="RGB888"):
    start(
        height=64,
        max_panels=max_panels,
        target_fps=target_fps,
        clock=clock,
        brightness=brightness,
        safe_mode=safe_mode,
        try_frames=try_frames,
        swap_frames=swap_frames,
        retry_delay_ms=retry_delay_ms,
        auto_reset=auto_reset,
        clear_results=clear_results,
        fmt=fmt,
    )


def run_pending():
    state = _load_state()
    if not state or not state.get("active"):
        print("No active panel sweep.")
        return False
    item = _measure_current_step(state)
    _continue_or_finish(state, item)
    return True


def resume_from_boot():
    state = _load_state()
    if not state or not state.get("active"):
        return False
    print("panel_sweep: resuming after reset")
    run_pending()
    return True


def abort():
    state = _load_state()
    if state:
        state["active"] = False
        state["finished_reason"] = "aborted"
        _save_state(state)
    print("panel_sweep: aborted")


def clear_results():
    _remove_file(RESULTS_FILE)
    print("panel_sweep: results cleared")


def show_results():
    state = _load_state()
    results = _load_results()
    if not results:
        print("panel_sweep: no results")
        return []

    print("-" * 50)
    print("panel_sweep results")
    print("-" * 50)
    best_fps = None
    best_theory = None
    best_bus = None
    for item in results:
        print(
            "{}x{} panels={} try={:.1f} swap={} refresh={} dma={} bus={} passed={}".format(
                item.get("width"),
                item.get("height"),
                item.get("panels"),
                item.get("try_show_fps") if item.get("try_show_fps") is not None else 0.0,
                item.get("swap_fps"),
                item.get("refresh_rate"),
                item.get("max_panels_theoretical_safe"),
                item.get("max_panels_by_bus"),
                item.get("passed"),
            )
        )
        if item.get("passed"):
            best_fps = item
        if best_theory is None or (item.get("max_panels_theoretical_safe") or 0) > (best_theory.get("max_panels_theoretical_safe") or 0):
            best_theory = item
        if best_bus is None or (item.get("max_panels_by_bus") or 0) > (best_bus.get("max_panels_by_bus") or 0):
            best_bus = item

    if best_fps:
        print("-" * 50)
        print(
            "Measured so far      : {}x{} (panels={}) try_show_fps={:.1f}".format(
                best_fps.get("width"),
                best_fps.get("height"),
                best_fps.get("panels"),
                best_fps.get("try_show_fps"),
            )
        )
    if best_theory:
        print(
            "Theoretical by DMA  : panels={} (total_limit={}, block_limit={})".format(
                best_theory.get("max_panels_theoretical_safe"),
                best_theory.get("max_panels_by_total_dma"),
                best_theory.get("max_panels_by_largest_block"),
            )
        )
    if best_bus:
        print(
            "Theoretical by bus  : panels={} at target_fps={}".format(
                best_bus.get("max_panels_by_bus"),
                best_bus.get("target_fps"),
            )
        )
    if best_fps and best_theory and best_bus:
        actual = min(
            best_fps.get("panels") or 0,
            best_theory.get("max_panels_theoretical_safe") or 0,
            best_bus.get("max_panels_by_bus") or 0,
        )
        if state and state.get("active"):
            print("Current practical   : {} panel(s) so far at target FPS".format(actual))
            print("Sweep status        : still active, reset and continue for final answer")
        else:
            print("Final practical     : {} panel(s) at target FPS".format(actual))
    return results


if __name__ == "__main__":
    start_64x32_chain(auto_reset=False)
