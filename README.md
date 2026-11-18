# RP2350 eyeballs

Firmware for an eyeball pendant on the Waveshare RP2350-Touch-AMOLED-1.8.
The plan lives in the "RP2350 Eyeballs" Claude project.

## Layout

| Path | What it is |
| --- | --- |
| `firmware/board/` | Board support: display, QSPI, touch, IMU, power button, text drawing |
| `firmware/eye/` | The eye renderer (`eye.c`), behaviour (`behave.c`), motion reactions (`motion.c`) and built-in styles. Portable C, no board code |
| `firmware/common/` | Puts an eye on the panel using both processor cores. Shared by the firmwares below |
| `firmware/pendant/` | The pendant firmware: a living eye at 30 frames per second |
| `firmware/eyetest/` | Render benchmark: the eye running flat out with a frame-rate readout |
| `firmware/bringup/` | Hardware test |
| `art/` | Source art in Monster Eyes format (config plus bitmaps), one folder per style |
| `tools/eyepack.py` | Converts a folder in `art/` into `firmware/eye/styles/<name>.c` |
| `tools/sim/` | Renders the eye on the Mac to image files, for checking without flashing |
| `dist/` | Prebuilt firmware, ready to flash |
| `build.sh` | Builds everything into `build/` and refreshes `dist/` |

## Flash a firmware

1. Hold the BOOT button on the board and plug it into the Mac over USB-C. A drive named `RP2350` appears.
2. Drag a `.uf2` file from `dist/` onto that drive. The board reboots by itself.

## `pendant.uf2`: the pendant

An eye that glances around, blinks and moves its eyelids by itself, held at a steady 30 frames per second. The sequence is different every power-on.

There are 12 styles: hazel, big_blue, anime, fizzgig, snake_green, demon, doom-red, skull, hypno_red, toonstripe, doom-spiral and spikes.

Touch is locked until you ask, so clothes and hugs change nothing:

| Do this | Result |
| --- | --- |
| Press and hold for about a second | Unlock. `UNLOCKED` shows under the eye. Hold again to lock |
| Swipe left or right (unlocked) | Next or previous style; its name shows for a moment |
| Swipe up or down (unlocked) | Brighter or dimmer, in six steps |
| Tap (unlocked) | Switch between one big eye and a pair of eyes |
| Double tap (unlocked) | Show or hide the debug readout |
| Leave it alone for 10 seconds | Locks itself |

The style, layout and brightness are remembered while the board is off. A red `LOW BATTERY` appears under the eye once a minute when the battery is at 10% or less.

It reacts to deliberate movement:

| Do this | The eye |
| --- | --- |
| Tilt it and hold | Looks toward the ground, then drifts back over about 10 seconds |
| Turn it quickly | Lags behind, then catches up |
| Shake it | Goes wide-eyed with a tiny pupil, then rolls dizzily for a couple of seconds |
| Leave it still for 40 seconds | Gets drowsy; at 60 seconds it closes and the pendant powers off |

On USB power the board cannot turn itself off, so it goes dark and wakes when moved.

The debug readout's top line is the motion sensor's reading (x, y, z in g, in screen directions), a letter for the current reaction (`S` shaken, `Z` dizzy, `D` drowsy) and the battery percentage. The bottom line is the frame rate and `BUSY`, the milliseconds of each 33ms frame spent working.

Thresholds and timings are named constants at the top of `firmware/eye/motion.c` and `firmware/eye/behave.c`. `tools/sim/motion_check.c` runs the motion detector against made-up walking, tilting, turning and shaking.

Not built yet: heart and star pupils, googly and robot eyes.

## `eyetest.uf2`: the render benchmark

The eye runs a fixed loop as fast as it can. The yellow line shows frames per second and milliseconds per frame. Tap to switch between `FULL` (edge shading and highlight) and `PLAIN`.

Measured on the first board:

| | `FULL` | `PLAIN` |
| --- | --- | --- |
| One core | 15.5 to 19 fps | 19 to 24 fps |
| Two cores, tighter loop | 35 to 42 fps | 44 to 54 fps |

## `bringup.uf2`: the hardware test

Measured on the first board: 113 fps full-screen fill at 37.4 MB/s, 138 fps for a 368x368 window.

| Step | Expected | Report back |
| --- | --- | --- |
| Colour fills | Red, green, blue, white, each full screen | Wrong colours or a partial fill |
| `FULL` and `EYE368` lines | Frames per second and MB/s | Both numbers |
| Two squares | A green square (even coordinates) and a pink one (odd coordinates) | Whether the pink square is clean, shifted, torn or missing |
| `IMU` / `TOUCH ID` | `IMU OK` and a non-zero ID | Any `FAIL` |
| `ACC` | Three numbers that change as you tilt the board | Which axis follows which tilt |
| `TOUCH` | Coordinates under your finger | Where 0,0 is |
| `PWR BTN` | Counts go up on short and long presses | Whether a long press also powers the board off |
| `BATTERY` | A plausible percentage with the battery attached | The value |
| Hold a finger for 3 seconds | On battery only, the board powers off | What wakes it again: the PWR button, plugging in USB, or movement |

The same values print over USB serial.

## Build it yourself

Install the toolchain once:

```sh
brew install cmake ninja
brew install --cask gcc-arm-embedded
```

Then run `./build.sh`. The first run downloads the Pico SDK unless `PICO_SDK_PATH` is set.

## Add or change eye styles

Each folder in `art/` is one style in Monster Eyes format: a `config.eye` file plus bitmaps.

```sh
pip3 install pillow numpy
tools/pack_all.sh
```

This rebuilds every file in `firmware/eye/styles/` and the list in `index.c`. The `STYLES` line at the top of `tools/pack_all.sh` chooses which styles are included and their swipe order. Each style also gets two preview images.

Supported from `config.eye`: textures, plain colours, eye and iris size, pupil range, slit pupils, eyelid shapes (or none), and spin. Spin turns the whole eyeball, not only the iris.

`tools/sim/run.sh` renders test poses into `tools/sim/out/`. It can also render frames of the idle behaviour, one frame of every style, or every style as a pair; see the comment at the top of `tools/sim/sim.c`.

## Where the art came from

Everything in `art/` is from Adafruit's [M4_Eyes](https://github.com/adafruit/Adafruit_Learning_System_Guides/tree/main/M4_Eyes/eyes) collection (repository license: MIT).

## Where the board code came from

`firmware/board/` holds Waveshare's driver files for this board (each carries Waveshare's permissive license header), copied from the community project [artsvuni/rp2350-doom](https://github.com/artsvuni/rp2350-doom), which fixed several reliability problems in them. That project has no license file for its own changes. This is fine for a personal build; before publishing this repo, re-base these files on Waveshare's official demo zip:
https://files.waveshare.com/wiki/RP2350-Touch-AMOLED-1.8/RP2350-Touch-AMOLED-1.8.zip

Local changes: the speaker amplifier pin is always held off, include paths were adjusted, a buffer overrun in `Paint_Clear` for 16-bit colour was fixed, and the display driver gained a non-blocking stream write.
