# WASABI338 TAB5

A fork of [zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026](https://github.com/zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026)
for the M5Stack Tab5 (ESP32-P4), restructured for PlatformIO, adding
a ReBirth338-style dual acid-303 + 808 engine.

All credit for the original 16-track sampler/synth/sequencer/FX engine,
touchscreen UI and USB MIDI host goes to the upstream author (zcarlos /
zircothc) — see their [README](https://github.com/zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026#readme)
for the full feature set of the base machine.

## What this fork adds

### USB keyboard (`src/usb_keyboard.h`)

Plug a plain USB keyboard into the Tab5's host port and it becomes a playable
controller. The USB host enumeration now claims either a MIDI device *or* an
HID boot keyboard, whichever is plugged in.

| Keys | Action |
|---|---|
| `z s x d c v g b h n j m` | Piano, lower octave (C C# D D# E F F# G G# A A# B) |
| `q 2 w 3 e r 5 t 6 y 7 u` | Piano, the octave above |
| `[` / `]` | Octave down / up |
| `Space` | Play / stop |
| `Enter` | Record on/off (starts playback if stopped) |
| `Esc` | Stop |
| `↑` / `↓` | Selected parameter +1 / -1 |
| `→` / `←` | Selected parameter +10 / -10 |
| `F1`–`F8` | Select track 0-7 (with Shift: 8-15) |
| `L` | Arm learn (same as the on-screen LEARN button) |

Notes played while recording are written into the current step, same as the
on-screen piano mode.

> **One port, one device.** The Tab5 has a single USB host port and the
> enumeration claims a single interface, so a MIDI controller and a keyboard
> can't be used at the same time without a hub plus multi-device support.

### MIDI learn / key learn (`src/midi_learn.h`)

Bind any incoming MIDI CC — or any USB keyboard key — to any of the machine's
parameters, saved to flash so bindings survive a reboot.

- Tap **LEARN** on the GLOBAL page (or press `L` on a USB keyboard)
- Select the parameter you want
- Move a knob on your controller, or press a key → bound
- Tap **LEARN** again to cancel, **SHIFT + LEARN** to clear the selected
  parameter's binding

A learned CC is looked up first; anything unlearned still falls through to the
upstream fixed AKAI APC KEY25 page/CC mapping, so an APC keeps working exactly
as it did. A learned *key* selects its parameter (so the arrow keys / on-screen
+1/-1 then act on it).

### ReBirth338 — dual acid-303 + 808 kit (`src/rebirth338.h`)

Two independent, procedurally-synthesized (no samples) TB-303-style acid
bass voices plus a 9-voice TR-808-style drum kit (kick, snare, clap, closed
hat, open hat, low tom, hi tom, cowbell, rimshot), all analog-modeled from
scratch (oscillators + envelopes + filter sweeps, not samples). It runs as
an always-on subsystem mixed straight into the master bus, independent of
the existing 16 sample/synth tracks — this keeps it decoupled from the
original's rotary-encoder/page UI framework, which is intentionally left
untouched.

- Boots with a demo groove already running (kick/hat pattern + an acid
  bassline riff on 303 A) so it's audible with no MIDI gear attached.
- Fully playable live over USB MIDI:

  | MIDI channel | Controls |
  |---|---|
  | 3 | Acid 303 A (note = pitch, velocity ≥ 100 = accent) |
  | 4 | Acid 303 B |
  | 10 | 808 kit (General-MIDI-style drum notes: 36 kick, 38/40 snare, 39 clap, 42/44 closed hat, 46 open hat, 41/43 low tom, 45/47/48/50 hi tom, 56 cowbell, 37 rimshot) |

  CC on channels 3/4: `74` cutoff, `71` resonance, `73` env mod, `72` decay,
  `91` accent amount.

### Input status panel

The GLOBAL page shows a live panel (right of the LEARN / USB KBD buttons)
with the state of the USB input path, so debugging a controller doesn't need
a serial console:

- `USB: MIDI ready` or `USB: HID x2 rx:1234` — interface count and report
  total
- With **USB KBD** toggled on, the raw HID report bytes underneath

### Bluetooth — removed for now

Bluetooth MIDI and a Bluetooth keyboard (HID-over-GATT central) were built
and confirmed compiled into the firmware, but boot-looped the device, so they
have been removed. The working implementation is preserved in git at commit
`ef1e694` together with the 16MB partition table it required.

What it took to get Bluetooth building, for whenever it's revisited:

- The ESP32-P4 has **no radio of its own** — BLE only exists via the onboard
  ESP32-C6, bridged over Espressif's ESP-Hosted transport on SDIO
  (GPIO 12/13/11/10/9/8, reset 15), which must be set via
  `BLEDevice::setPins()` before init.
- Needs **arduino-esp32 3.3.4+** (pioarduino 55.03.35+). Earlier versions
  guard `BLEDevice` on `SOC_BLE_SUPPORTED` alone, false on the P4, so
  Bluetooth cannot work on them no matter what sdkconfig says.
- Do **not** set `custom_sdkconfig`. The stock esp32p4 libs already define
  `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`; setting it forces hybrid IDF
  compilation that regenerates a config without it, silently disabling BT.
- The `.ino` → `.cpp` conversion preprocesses before library include paths
  resolve, so the BLE library must be added to the include path explicitly.
- The BLE stack adds ~510KB, overflowing the default ~1.28MB app partition.
- Suspected boot-loop cause: a failed C6 handshake can `abort()` rather than
  return an error. The Tab5 ships ESP-Hosted **slave 1.4.1** against a much
  newer host, so updating the C6 firmware is the first thing to try.

## Building

```
platformio.ini
```
targets `esp32-p4-evboard` via the `pioarduino` fork of `platform-espressif32`
(per the [official Tab5 PlatformIO guidance](https://docs.m5stack.com/en/core/Tab5)),
pinned to 55.03.35 = arduino-esp32 3.3.5 + ESP-IDF 5.5.1 (5.5.2 introduced a
MIPI-DSI backlight flicker on this panel). `lib/esp32_usb_host_demos/` vendors two small
MIT-licensed header files from
[touchgadget/esp32-usb-host-demos](https://github.com/touchgadget/esp32-usb-host-demos)
that the USB MIDI host code depends on (that repo isn't a proper PlatformIO
library, so the two headers are vendored directly rather than pulled as a
dependency).

## Known limitations

- ReBirth338 mixes into the master dry bus only — it doesn't (yet) route
  through the per-track FX sends (reverb/delay/chorus/etc.) that the 16
  sample/synth tracks use.
- ReBirth338's patterns/parameters aren't exposed on the touchscreen UI yet
  — MIDI is the only way to play/control it for now.
- Bluetooth is removed for now (see above) after it boot-looped the device.
