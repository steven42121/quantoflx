# ScreenBrightness Python daemon (alternative implementation)

An alternative way to drive QuantoFlx `ScreenBrightness` brightness sliders
without a compiled Rainmeter plugin, for setups where the stock `Backlight`
plugin cannot work.

## Why this exists

`Backlight.dll` locates the monitor by "the screen the skin is on". If the
skin sits on a monitor that has no DDC/CI support (e.g. a vertical panel),
every read/write fails ("unavailable on your device"), and its `Monitor=N`
index is also unreliable in that case.

This implementation bypasses the plugin entirely:

- **`brightness_helper.py`** (Python 3) talks to monitors via the Windows
  dxva2 API with a **global** physical-monitor index (independent of where
  the skin is placed).
- **`bri_feed.lua`** (Rainmeter script measure) bridges values into skin
  variables (`Bri0` / `Bri1` / `Bri2`) and forwards user actions back to the
  daemon.

## Architecture

```
read :  helper daemon  --writes-->  @Resources\bri_state.txt
          ^                              |
          |                         lua Update()
          |                              v
        skin variables  Bri0/1/2  -->  sliders

write:  slider action --> lua Set()/Bump()
          |-- optimistic SetVariable + Redraw   (slider responds <100 ms)
          '-- appends to @Resources\bri_cmd.txt
                 |
                 v
              daemon (30 ms poll, queue collapsed per monitor)
                 '-- dxva2 SetMonitorBrightness --> then publish state
```

Key details that make it feel fast:

- **Optimistic update**: the lua `Set`/`Bump` handlers move the slider
  immediately; DDC write happens afterwards and the next reading realigns
  the value if the hardware clamped it.
- **Command queue** (`bri_cmd.txt`) is append-only and drained in one pass,
  collapsed to one final absolute value per monitor so rapid drag/scroll
  does not spam DDC.
- **Idle pause**: after a write the daemon aligns for ~1.5 s, then sleeps
  (no hardware polling). It also polls `bri_cmd.txt` only for file
  existence.
- **Lifecycle**: started from the skin (pid-file guarded, so a refresh is
  safe); it exits when Rainmeter is no longer running (kernel32 process
  snapshot, *no* `tasklist` subprocess — that used to flash console
  windows).

## Files

| File | Role |
|---|---|
| `brightness_helper.py` | dxva2 read/write + `daemon` mode + one-shot `get`/`set`/`bump` |
| `bri_feed.lua` | skin-side bridge (state file -> variables, actions -> command file) |
| `sample.bri_state.txt` | example of the 3-value state line (`<bri0> <bri1> <bri2>`, `-1` = unavailable) |

## How to wire it into `4x2_A.ini` (or similar)

1. Copy `brightness_helper.py` and `bri_feed.lua` next to the skin ini
   (e.g. `Skins/QuantoFlx/Package/QuantoFlx.System/ScreenBrightness/`).
2. Add the script measure:

```ini
[Ms.BriFeed]
Measure=Script
ScriptFile=bri_feed.lua
UpdateDivider=1
```

3. Set `Update=250` or faster under `[Rainmeter]` (optimistic UI makes
   this less critical, but the state-file catch-up uses it).
4. Display `#Bri0#` / `#Bri1#` / `#Bri2#` in the sliders and treat
   `BriN = -1` as "unavailable".
5. Point actions at the lua functions (note the **Lua-call syntax**):

```ini
MouseScrollUpAction=[!CommandMeasure "Ms.BriFeed" "Bump(0, 2)"]
MouseScrollDownAction=[!CommandMeasure "Ms.BriFeed" "Bump(0, -2)"]
IfTrueAction=[!CommandMeasure "Ms.BriFeed" "Set(0, [Ms.Per_0:%,0_0])"]
```

6. Start the daemon once from the skin, e.g. in a script `Initialize()`
   (see `bri_feed.lua`) with `pythonw.exe ... brightness_helper.py daemon`.

### Traps we hit (save yourself the time)

- Command-line `!SetVariable` sent from an **external process** never
  reaches the skin; use `SKIN:Bang` from Lua instead.
- `SKIN:GetVariable("@")` is `@Resources`, not the skin folder.
- In Lua source, Windows paths must not sit in plain `'...'` strings
  (`\U` is an invalid escape); use `[[...]]` / `[=[...]=]` or double the
  backslashes.
- `!CommandMeasure` on a script measure runs **Lua code** —
  `"Set 0 70"` is a syntax error; it must be `"Set(0, 70)"`.
- Do not launch anything with a visible console from a `pythonw` daemon
  (e.g. `tasklist`); it flashes windows on the desktop every few seconds.

## Relationship to the plugin implementation

The compiled `Brightness.dll` plugin is the preferred, lower-overhead
solution. Keep this directory as a **reference / fallback** for cases where
a plugin cannot be built or when debugging the same logic is easier in
Python. They are not meant to run side by side on the same skin.
