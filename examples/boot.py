# Copy this file to the board root as /boot.py.

import gc

import display_session


display_session.boot_prepare()
gc.collect()
print("hub75 boot ready")
print("display session module loaded")
