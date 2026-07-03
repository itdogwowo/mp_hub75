# [OPEN] hub75-status-crash

## Symptom
- Device disconnects during `safe_start()` before `begin()`.
- Output reaches `status(before begin): {...` and then USB serial disappears.

## Current Scope
- Project: `/Users/user/Documents/code/git/mp_hub75`
- Focus: `Display.status()` and pre-`begin()` attribute access path

## Hypotheses
1. `status()` touches a bridge getter that is not safe before `begin()`.
2. `status()` allocates or formats an object in a way that triggers a crash on-device.
3. A specific attribute in `status()` causes invalid access when `handle->driver == NULL`.
4. The crash occurs during REPL formatting of the returned dict, not during dict creation itself.
5. `display_attr` or `status()` indirectly calls a bridge path that assumes started hardware.

## Evidence Needed
- Determine whether crash happens on `display.status()` call or while printing the returned dict.
- Identify which field inside `status()` is the first unsafe access.
- Confirm whether direct attribute reads reproduce the same disconnect.

## Evidence Collected
- `display.status()` as a whole is unsafe before `begin()`.
- Direct attribute probing narrowed the crash boundary into string-style status fields.
- `display.last_stage` printed successfully with value `created`.
- A later run crashed while reading `display.last_error`, before reaching `buffer_info()` or `begin()`.
- Therefore the debug surface itself is interfering with bring-up, so the example should avoid pre-`begin()` string/status access until core display flow is validated.
- `display.buffer_info()` also crashed before `begin()`, which rules out DMA-startup conflicts because this path only performs size estimation and MicroPython object construction.
- Current leading hypothesis: pre-`begin()` crashes come from the extension's dict/string object-building path rather than hardware initialization.
- Output ordering is ambiguous because `print()` may not fully flush before a native reset, so Python-side breadcrumbs need short delays to establish the true crash boundary.

## Plan
- Remove pre-`begin()` status probing from the example and keep only core checkpoints.
- Rebuild and verify whether execution now reaches `buffer_info()`, `begin()`, and `show()`.
- If core flow succeeds, revisit and harden the diagnostics API separately.
- Harden `buffer_info()` first by returning a minimal tuple instead of a dict.
- Replace verbose example prints with short stage markers plus short sleeps to improve breadcrumb reliability across USB disconnects.
