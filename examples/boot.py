# Copy this file to the board root as /boot.py.

import gc

import display_session


display_session.boot_prepare()
gc.collect()
print("hub75 boot ready")
print("display session module loaded")

try:
    import panel_sweep
except Exception:
    panel_sweep = None

if panel_sweep is not None:
    try:
        panel_sweep.resume_from_boot()
    except Exception as exc:
        print("panel_sweep resume error:", exc)
