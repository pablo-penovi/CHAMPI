# CHAMPI implementation plan

CHAMPI is a native Linux virtual instrument that runs the real CHOMPI TAPE firmware on x86.
The design is in [PORT.md](../PORT.md). This file splits that design into chunks. Each chunk ends
with something that builds, has tests, and gets committed and pushed.

## Progress

Tick a chunk's box when its PR is merged into `main`.

| Done | # | Chunk | Done when |
|:---:|---|---|---|
| ☐ | 0 | Repo skeleton: the CHOMPI and DPF submodules, CMake, README, licence and trademark note | An empty build runs and is pushed |
| ☐ | 1 | The real TAPE firmware sources compile and link against placeholder hardware stand-ins | No missing pieces at link time |
| ☐ | 2 | Virtual SD card: FatFS on a disk-image file, formatting, seeding with the factory card, import/export | Format, seed and read-back test passes |
| ☐ | 3 | Models of each piece of hardware (shift registers, encoders, battery charger, LEDs), no threading yet | Unit tests pass, using the firmware's own encoder and LED code |
| ☐ | 4 | Emulated chip that runs the firmware's interrupts in priority order, plus the headless runner | Recorded-output tests pass (boot, playing keys, encoders, presets) and sanitizers are clean |
| ☐ | 5 | DPF app with audio and MIDI only, and a rough placeholder screen | A MIDI controller plays it through JACK with no audio dropouts |
| ☐ | 6 | The real panel: vector drawing, layout taken from the `.brd` files, LED rendering, mouse control | It looks like the reference image and plays fully by mouse |
| ☐ | 7 | Computer keyboard control with a configurable keymap | It plays fully without the mouse |
| ☐ | 8 | Polish: shift menus, looper and recording, test mode, removing the SD card, encoder feel, README | Everything in PORT.md's milestone 5 is covered |
| ☐ | 9 | Later: TEMPO and WAVE | |

Biggest risks: chunk 1 (the bundled libDaisy is a modified fork, so the stand-in may grow) and
chunk 4 (signal-based interrupt emulation is the most fragile part; a single-lock fallback is
described below).

## Repo layout

PORT.md puts the port inside the CHOMPI tree at `ports/linux/`. Here it lives in its own repo,
so the layout moves up one level:

```
CHAMPI/
  CMakeLists.txt
  third_party/
    CHOMPI/        git submodule -> CHOMPI-Club/CHOMPI (firmware sources, card profiles, .brd files), never patched
    DPF/           git submodule -> DISTRHO/DPF
  host-daisy/      libDaisy hardware shim
  core/            virtual hardware + firmware build + runtime
  app/             DPF plugin + NanoVG UI
  tools/           champi-headless, panel layout extractor
  tests/
  docs/            PORT.md
    planning/      OVERALL_PLAN.md (this file)
```

## Chunk details

### 0. Repo skeleton
- Add the submodules: CHOMPI pinned to `a73d732`, and DPF pinned to a release tag.
- Add a top-level CMake file with the `CHOMPI_FIRMWARE=tape` option, gnu++17, and warnings set
  low for the firmware sources only.
- Add the README (what this is, the trademark note, how to build), `.gitignore`, and an MIT
  LICENSE with credit to the CHOMPI Club code.
- **Done when:** `cmake -B build && cmake --build build` runs an empty build, and the repo is
  pushed to GitHub.

### 1. Compile and link TAPE against a stub shim
- Write `host-daisy/include/...`, matching the vendored libDaisy fork's headers one for one.
  Bodies are empty stubs for now.
- Handle the portability snags on the shim side: the `Limiter.h` forwarder, empty section
  macros, `-Dmain=chompi_fw_main`, and HAL/CMSIS stubs.
- Build `core/firmware_tape` from the unmodified sources, together with DaisySP, coreJSON,
  libDaisy `ui/`, `util/` and `hid/midi_parser`.
- **Done when:** `libchampi_fw_tape.a` links into a test binary that references
  `chompi_fw_main`, with no undefined symbols.
- **Risk:** this chunk shows how big the shim really is. If the fork's headers drift a lot
  from upstream, the scope may grow here.

### 2. Virtual SD card
- Build the vendored `ff.c` with its `ffconf.h`, plus a host `diskio` that does
  `pread`/`pwrite` on an image file.
- Create the image: sparse 4 GB, formatted with `f_mkfs`, and seeded from
  `card-profiles/tape-2.0/`.
- Add the CLI commands `--sd-import`, `--sd-export` and `--sd-reset`.
- **Done when:** a unit test runs mkfs, seeds the image, reads it back, and the file list
  matches the card profile.

### 3. Peripheral models (pure logic, no threads)
- `PanelState` atomics and the `LedFrame` triple buffer.
- A ShiftRegister4021 model with the fork's debounce, `RawState` and edge detection.
- A GPIO pin table.
- An EC12 quadrature generator.
- An MP2722 register model.
- A WS2812 PWM/DMA decoder.
- **Done when:** unit tests pass for the encoder through the real `ChompiEncoder`, the 4021
  debounce, the `fill_led_data` round trip, and the MP2722 lockout check.

### 4. Virtual MCU and headless runner (milestone 1 of PORT.md)
- The MCU thread, with interrupts delivered as real-time signals at audio > TIM4 > LED-DMA
  priority.
- `__disable_irq` mapped onto the signal mask.
- The 1 kHz `timer_create` for TIM4.
- The SPSC audio ring and the block adapter.
- `champi-headless`: script in, WAV and LED log out.
- **Done when:** the headless golden tests pass (boot rainbow in the LED log, KEY1 plays the
  slot-1 sample at the right pitch, an encoder turn changes the pitch, a preset save survives a
  restart), and the suite is clean under TSan and ASan.
- **Risk:** this is the hardest chunk. If signal delivery turns out unworkable (for example, an
  ISR touches state that isn't async-signal-safe), the fallback is a single big-lock
  "interrupt runner" thread that pre-empts the main loop at the firmware's own poll points.
  That would cost a little fidelity.

### 5. DPF app: audio and MIDI only
- The `ChompiPlugin` standalone JACK target, with 3 inputs and 4 outputs, and auto-connect for
  master out.
- JACK MIDI in and out, merged into the virtual TRS UART, plus an optional ALSA-seq
  auto-connect.
- libsamplerate when the host rate is not 48 kHz.
- A placeholder UI: plain rectangles for the keys, LED colours, and a CPU-load readout.
- **Done when:** a MIDI controller plays the instrument through PipeWire-JACK, and there are no
  xruns at a 64-frame buffer.

### 6. Panel UI
- A layout extractor that reads the enclosure and main-board `.brd` files and writes
  `panel_layout.h`.
- A NanoVG vector panel styled after `interface.jpg`, with a fixed-aspect resizable window.
- LED rendering: undo the firmware's scaling, apply gamma, add a glow.
- Mouse input: keys, encoder drag, scroll and push, the toggle switch, and the jack-detect
  toggle.
- An optional user `skin/` folder; no art is committed.
- **Done when:** the panel matches the reference side by side and is fully playable with the
  mouse.

### 7. Computer keyboard
- Scancode mapping, with defaults from PORT.md §4.
- `~/.config/champi/keymap.toml`.
- Encoder select, turn and push from the keyboard.
- **Done when:** everything on the panel can be played without the mouse.

### 8. Fidelity and polish (milestone 5 of PORT.md)
- Check the shift-menu flows, the looper, and recording through mic and line-in.
- Options persistence.
- Test mode (ENC6 held at boot).
- Simulated SD-card removal.
- Encoder feel and acceleration tuning.
- Write the full README.

### 9. Later: TEMPO and WAVE
- A shim superset: TIM16 MIDI clock, `MidiManager` DMA transmit, `f_opendir`/`readdir`.
- Their card profiles and the libDaisy fork headers for each.
- Probably one chunk per firmware.

## Working agreement

- Firmware comes from an upstream submodule (`CHOMPI-Club/CHOMPI` @ `a73d732`) and is never patched.
- The product name is `champi`: binaries `champi` and `champi-headless`, config in `~/.config/champi`, data in `~/.local/share/champi`. Wherever PORT.md says `chompi-linux`, read `champi`.
- The GitHub repo `pablo-penovi/CHAMPI` is private.
- Each chunk gets its own branch (`chunk-N-<slug>`) and a PR into `main`. You review and merge.
- After opening each PR I stop and summarise what changed and anything that deviated from PORT.md.
