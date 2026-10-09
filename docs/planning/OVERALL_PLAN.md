# CHAMPI implementation plan

CHAMPI is a native Linux virtual instrument that runs the real CHOMPI TAPE firmware on x86.
The design is in [DETAILED_OVERALL_PLAN.md](DETAILED_OVERALL_PLAN.md) (formerly `PORT.md`). This
file splits that design into chunks. Each chunk ends with something that builds, has tests, and
gets committed and pushed.

The libDaisy replacement (the "shim" in the detailed plan) and the virtual MCU now live in a
separate repo, [daisycola](https://github.com/pablo-penovi/daisycola), pulled in as a submodule.
daisycola handles the generic Daisy parts: replacement libDaisy headers, interrupts, timers, GPIO,
4021 shift registers, I2C, PWM DMA, the SD-card image and audio/MIDI plumbing. CHAMPI keeps
everything specific to the CHOMPI board and the app: panel wiring, the MP2722 charger model, the
WS2812 LED layout, the headless runner, DPF and the UI. Chunks 1–4 are split between the two repos;
the daisycola phase each one needs is in brackets.

## daisycola status (2026-10-08)

daisycola phases 1–5 are done and pushed (`e9ab6a0`). TAPE links against it unchanged, boots
headless from the factory card on the signal-based virtual MCU, and the suite is clean under TSan
and ASan. So the daisycola side of chunks 1–4 is finished and the CHAMPI side of each is smaller
than first planned. Chunk 4 closed the last phase 5 check: using `daisycola/host.h` for real only
turned up the audio bug below. Phase 6 (TEMPO and WAVE) is for later, with chunk 10.

Chunk 3 moved CHAMPI's pin to `7230242`: I2C `TransmitBlocking` now waits for a DMA read running
on the same bus, as libDaisy's does. TAPE's medium-battery check depends on that.

Chunk 4 moved it to `6a8bc75`, which fixes the 24-bit sample round trip. libDaisy's `s242f`
sign-extends a raw 24-bit word, but daisycola passed it the whole int32, so every negative sample
came out about 2.0 too low. That closes phase 5: `host.h` needed nothing else to run TAPE for real.

Where building daisycola changed the detailed plan, daisycola's
[design notes](https://github.com/pablo-penovi/daisycola/blob/main/docs/design-notes.md) win. In
short:

- Interrupt priorities follow the NVIC: audio, I2C, UART, LED DMA and USB are all priority 0 and
  don't preempt each other; the timers are priority 15. Not "audio, then TIM4, then the rest".
- TIM4 runs at about 4.4 kHz, not 1 kHz, because libDaisy truncates TAPE's period to 16 bits. The
  device does the same.
- The WS2812 decoder counts a pulse longer than half a bit as 1 (the ⅔ rule misreads TAPE), and
  returns exactly the 25 SMT and 10 PTH LEDs, porch slots dropped. SMT is GRB, PTH is RGB.
- Encoders step one Gray-code state every 3 ms, not one per poll.
- The 4021 chips are modelled at the pin level; the fork's debounce code runs as written.
- MIDI in raises the UART/USB receive interrupt.
- The single-lock fallback was never needed and doesn't exist.
- A halted firmware can't restart: one firmware run per process.
- Under ASan, the host executable must set `protect_shadow_gap=0` (via `__asan_default_options` or
  the environment) for TAPE's raw SDRAM writes.

## Progress

Tick a chunk's box when its PR is merged into `main`.

| Done | # | Chunk | Done when |
|:---:|---|---|---|
| ☑ | 0 | Repo skeleton: the CHOMPI, daisycola and DPF submodules, CMake, README, licence and trademark note | An empty build runs and is pushed |
| ☑ | 1 | The real TAPE firmware sources compile and link against daisycola's stubs [daisycola 1] | No missing pieces at link time |
| ☑ | 2 | Virtual SD card: seeding the daisycola image with the factory card, import/export commands [daisycola 2] | Format, seed and read-back test passes |
| ☑ | 3 | CHOMPI board model on top of daisycola: key and encoder wiring, battery charger, LED layout; no threading yet [daisycola 3] | Unit tests pass, using the firmware's own encoder and LED code |
| ☑ | 4 | Firmware running on daisycola's virtual MCU, plus the headless runner [daisycola 4] | Recorded-output tests pass (boot, playing keys, encoders, presets) and sanitizers are clean |
| ☑ | 5 | DPF app with audio and MIDI only, and a rough placeholder screen | A MIDI controller plays it through JACK with no audio dropouts |
| ☑ | 6 | The real panel: vector drawing, layout taken from the `.brd` files, LED rendering, mouse control | It looks like the reference image and plays fully by mouse |
| ☑ | 7 | Computer keyboard control with a configurable keymap | It plays fully without the mouse |
| ☑ | 8 | Polish: shift menus, looper and recording, test mode, removing the SD card, encoder feel, README | Everything in the detailed plan's milestone 5 is covered |
| ☐ | 9 | Connections menu: route CHAMPI's audio and MIDI ports to other JACK/PipeWire ports from an F8 overlay, saved and restored | Every port can be routed from the menu without qpwgraph, and the routing comes back on the next start |
| ☐ | 10 | Later: TEMPO and WAVE [daisycola 6] | |

Both original big risks are settled in daisycola: the fork turned out manageable (chunk 1), and the
signal-based interrupts work under both sanitizers (chunk 4). The biggest risk now is chunk 5:
real-time audio through JACK, with the firmware on its own thread fed by `ProcessAudio`, without
xruns at a 64-frame buffer.

## Repo layout

The detailed plan puts the port inside the CHOMPI tree at `ports/linux/`. Here it lives in its own repo,
so the layout moves up one level:

```
CHAMPI/
  CMakeLists.txt
  third_party/
    CHOMPI/        git submodule -> CHOMPI-Club/CHOMPI (firmware sources, card profiles, .brd files), never patched
    daisycola/     git submodule -> pablo-penovi/daisycola (libDaisy replacement + virtual MCU)
    DPF/           git submodule -> DISTRHO/DPF
  core/            firmware build, CHOMPI board model (wiring, MP2722, LED layout), runtime glue
  app/             DPF plugin + NanoVG UI
  tools/           champi-headless, panel layout extractor
  tests/
  docs/
    planning/      OVERALL_PLAN.md (this file), DETAILED_OVERALL_PLAN.md
```

## Chunk details

### 0. Repo skeleton
- Add the submodules: CHOMPI pinned to `a73d732`, daisycola pinned to `e9ab6a0`, and DPF pinned
  to `4238e1c` on `main` (DPF has no release tags).
- Add a top-level CMake file with the `CHOMPI_FIRMWARE=tape` option, gnu++17, and warnings set
  low for the firmware sources only (`champi_firmware_flags`, `-w`; CHAMPI's own code links
  `champi_warnings`). Point daisycola's `DAISYCOLA_CHOMPI_DIR` at `third_party/CHOMPI` (it
  derives the TAPE libDaisy fork from it), and turn `DAISYCOLA_BUILD_TESTS` off. DPF is added with
  `DPF_LIBRARIES` and `DPF_EXAMPLES` off: it only provides `dpf_add_plugin()`, which builds the
  DGL library a plugin needs.
- Add the README (what this is, the trademark note, how to build), `.gitignore`, and an MIT
  LICENSE with credit to the CHOMPI Club code.
- **Done when:** `cmake -B build && cmake --build build` runs an empty build, and the repo is
  pushed to GitHub. The "empty" build already compiles `libdaisycola.a`, which checks the CHOMPI
  path.

### 1. Compile and link TAPE against daisycola [daisycola phase 1: done]
- daisycola: done. Its `tests/tape/CMakeLists.txt` already builds TAPE this way and its
  `tape_link` test passes.
- CHAMPI: build `core/firmware_tape` the same way: DaisySP as its own library;
  `chompi_main.cpp`, `encoder.cpp`, `FileStreamingManager.cpp` and `core_json.c` with
  `-Dmain=chompi_fw_main`, both linking `champi_firmware_flags` privately for `-w`; linked to
  daisycola. daisycola's include directories must come
  before DaisySP's (it carries a `DelayLine` fix).
- **Done when:** `libchampi_fw_tape.a` links into a test binary that references
  `chompi_fw_main`, with no undefined symbols.
- As built: `core/CMakeLists.txt` defines `champi_daisysp` and `champi_fw_tape`, both compiled with
  `-w` through `champi_firmware_flags`. `champi_fw_tape` links daisycola before `champi_daisysp`
  publicly, so anything that links the firmware gets the right include order. The link test is
  `tests/fw_tape_link.cpp` (ctest `champi_fw_tape_link`), a plain program like daisycola's
  `tape_link`. GoogleTest comes in with chunk 2, the first chunk with real unit tests.

### 2. Virtual SD card [daisycola phase 2]
- daisycola: done. `SdCreateImage` (MBR plus one FAT32 partition), `SdOpenImage`, `SdList`,
  `SdCopyIn`/`SdCopyOut`, and `SdSetPresent` for removing the card.
- CHAMPI: create the image at `~/.local/share/champi/sdcard.img` (sparse 4 GB), seed it from
  `card-profiles/tape-2.0/`, and add `--sd-import`, `--sd-export` and `--sd-reset`.
- **Done when:** a unit test runs mkfs, seeds the image, reads it back, and the file list
  matches the card profile.
- As built: `core/sd_card.{h,cpp}` (`champi_sd`) has `CreateCard`, `EnsureCard`, `ImportToCard`
  and `ExportFromCard` over daisycola's helpers. A new image is built at `<image>.new` and renamed
  into place, so a failed reset keeps the old card. The whole profile is copied, including
  `CHOMPI_TAPEv2_0.bin`, as on a real card (TAPE ignores it). The path honours `$XDG_DATA_HOME`.
  `core/sd_cli.{h,cpp}` parses `--sd-image`, `--sd-reset`, `--sd-import` and `--sd-export` and
  runs them in that order, creating the card first if it doesn't exist. `champi-headless` exists
  as a stub that only runs these; chunk 4 adds the firmware run, and chunk 5's `champi` reuses
  `sd_cli`. The factory-card path is compiled in from the CHOMPI submodule; an installed build will
  need it installed with the binary. `champi-tests` (GoogleTest, fetched if not installed) holds
  the unit tests.

### 3. CHOMPI board model (pure logic, no threads) [daisycola phase 3]
- daisycola: done. Pin-level 4021 chains (`AttachSr4021`, `SetSrInputs`), time-stepped encoders
  (`AttachEncoder`, `QueueDetents`), `I2CDevice` registration, PWM-DMA capture (`GetDmaFrame`) and
  `Ws2812Decode`. Its tests already drive TAPE's own `ChompiEncoder` and LED driver, and its boot
  test has a minimal fake MP2722.
- CHAMPI adds what is specific to this board, on daisycola's public API only:
  - `PanelState`: which key and encoder sits on which 4021 chain and bit or GPIO pin (`SwId`,
    `EncoderSrId`, `seed::D0/D20/D10`, `D15/D17`, the toggle, jack-detect on `D21`), attached
    through `AttachSr4021` and `AttachEncoder`.
  - A full MP2722 register model at I2C address `0x3F`, growing from daisycola's test fake: VIN_GD
    (register 0x12, bit 6) set, `mpc_int` (D31) held high, and a battery level the UI can set.
  - The LED layout: decoded SMT (GRB) and PTH (RGB) LEDs mapped to panel positions in a
    `LedFrame`. Porch slots are already gone after decoding.
- **Done when:** unit tests pass for key presses through `SwId`, an encoder turn through
  `PanelState`, a decoded LED frame landing in the right `LedFrame` slots, and the MP2722 lockout
  check. daisycola's own `ChompiEncoder` and `fill_led_data` tests aren't repeated.
- As built: `champi_board` holds `core/panel_state`, `core/mp2722` and `core/led_frame`.
  - `PanelState` numbers keys and encoders as printed on the board (KEY1-28, ENC1-6) and keeps no
    state of its own: the levels live in daisycola, so any host thread can set or read them.
    `Attach` wires everything once per process (daisycola can't re-wire a chain). At rest every
    key is up, the toggle is on (what `GetToggleState` reports as true; the firmware only lights
    the encoder LEDs and opens the shift menu then) and the line-in jack is empty.
  - `Mp2722` computes VIN_GD, CHG_STAT and BATT_LOW_STAT from a power state the host sets (USB
    power, battery millivolts, charge done) and stores every other register. The BATT_LOW
    threshold is read from register 0x0C bits 3:2. TAPE's two values give 0b00 = 3.0 V and
    0b11 = 3.3 V; the steps between are assumed to be 100 mV. By default it's on USB with a full
    battery. A UI battery-level control maps onto these setters later.
  - `ReadLedFrame` decodes both chains into `LedFrame`: `key[0..27]` (KEY26-28 are PTH LEDs) and
    `encoder[0..5]`, plus `encoder5_second` for ENC5's other LED. The maps come from TAPE's
    `led_map`. The firmware's /4 and /11 scaling stays in, for chunk 6 to undo.
  - The tests (`tests/board_test.cpp`) drive TAPE's own `chompi::Hardware`: its real `Init`,
    `ProcessAllControls`, `SwId` names, `MpReadAll`, `BMCMediumBattCheck` and
    `LowBatteryLockoutCheck`, plus TAPE's LED driver. They run single-threaded on the manual
    clock. They check against the firmware's own enum names and a verbatim copy of `led_map`,
    not against CHAMPI's tables.
  - Writing them turned up the daisycola I2C ordering bug above. It was fixed there, not worked
    around here.

### 4. Firmware on the virtual MCU, plus headless runner (milestone 1) [daisycola phase 4]
- daisycola: done. The firmware thread, signal-based interrupts in NVIC priority order,
  `__disable_irq`, timers, the audio rings (internal clock for headless, `ProcessAudio` for a
  host), MIDI byte streams (`WriteMidiIn`, `ReadMidiOut`), STOP mode and `Wake`, and `Halt`.
- CHAMPI: start `chompi_fw_main` with `daisycola::Start`, connect the board model from chunk 3,
  and build `champi-headless`: script in, WAV and LED log out. daisycola has no event log, so the
  runner builds the LED log itself from `GetDmaFrame`.
- A firmware runs once per process, so the preset-survives-restart test runs two processes on one
  image.
- Add a `CHAMPI_SANITIZER` option (thread, address or empty) that sets the sanitizer flags for
  CHAMPI's targets and passes the same value to `DAISYCOLA_SANITIZER`. daisycola sets its flags
  with `add_compile_options`, which only reaches its own directory, so CHAMPI's targets (and the
  firmware sources) aren't instrumented unless CHAMPI adds the flags too.
- CHAMPI's ASan build defines `__asan_default_options` with `protect_shadow_gap=0`. Decide then
  whether daisycola should ship that as an opt-in `daisycola::asan_sdram` target instead.
- Feed back into daisycola anything `host.h` turned out to lack or get wrong; that closes daisycola
  phase 5.
- **Done when:** the headless golden tests pass (boot rainbow in the LED log, KEY1 plays the
  slot-1 sample at the right pitch, an encoder turn changes the pitch, a preset save survives a
  restart), and the suite is clean under TSan and ASan.
- As built:
  - `core/runtime` (`champi_runtime`) is the process's `Runtime`. `Start` inserts the card, wires
    `PanelState` and `Mp2722`, sets the audio clock and starts `chompi_fw_main`. `Booted` reads
    TAPE's own `booting` and `rainbow_done` flags. `Stop` waits for `SdBusy` to clear, halts the
    firmware and closes the image. Chunk 5 reuses it with `AudioClock::kHost`.
  - `__asan_default_options` (`protect_shadow_gap=0`) lives in `runtime.cpp`, so every program
    that runs the firmware gets it. daisycola doesn't ship an `asan_sdram` target: its own tests
    define the option themselves, and CHAMPI needs it in one place only.
  - `champi-headless --script <file> [--wav <file>] [--log <file>]` runs a line-based script
    (`tools/script.h`: `boot`, `wait`, `key`, `push`, `turn`, `toggle`, `linein`, `midi`, `usb`,
    `battery`, `mark`) in real time on daisycola's internal audio clock. The WAV is the master
    output (TAPE's outputs 3 and 4) as 32-bit float stereo. The log has a line per event,
    `<ms> <frame> <event>`: the script's commands, `booted`, and `leds` with all 35 colours
    whenever they change. So it's the LED log and the event timeline in one file.
  - TAPE ignores the panel for about 5 s after boot while the rainbow plays (16 ms redraws, as on
    the device). Scripts wait 6 s after `boot`.
  - After boot TAPE is in JAMMI mode on slot 15, its built-in sample: a C4 sine (261.63 Hz) on the
    right channel. The golden tests use that sample instead of slot 1, because its pitch is
    known exactly. KEY8 plays C4 and KEY1 plays C3. The tests check the pitch to 0.5%.
  - The speed knob is ENC4 (the firmware's encoder 0). The test turns it +20 and -40 detents and
    checks the pitch against TAPE's own speed curve, at 0.003 per detent.
  - The preset test is the device's own save flow: raise the speed, hold CHOMPI, press save
    (KEY25), pick slot 1 (KEY1), release CHOMPI, then press it again. It checks that
    `presets.json` and `jammi_a1.wav` changed on the card. A second process on the same image
    then selects slot 1 in the shift menu and checks KEY8 plays at the saved speed.
  - The golden tests (`champi-headless-tests`) run the real binary in a subprocess, so the
    sanitizer builds check it too. `CHAMPI_SANITIZER=thread|address` adds the flags at the top
    level, so they reach CHAMPI, the firmware and daisycola, and sets `DAISYCOLA_SANITIZER` to
    match. All 33 tests pass under both.
  - The master output peaks at about 0.027 when one key plays the built-in sample at the factory
    settings. Whether that matches the device is still to check.

### 5. DPF app: audio and MIDI only
- DPF's CMake is already loaded (chunk 0). `dpf_add_plugin(champi TARGETS jack ...)` builds
  `dgl-opengl` itself; the default OpenGL UI type needs the OpenGL and X11 development packages.
- The `ChompiPlugin` standalone JACK target, with 3 inputs and 4 outputs, and auto-connect for
  master out. Audio runs on daisycola's host clock: `run()` calls `ProcessAudio`, which adds two
  24-frame blocks (1 ms) of latency.
- JACK MIDI in and out, merged into the virtual TRS UART, plus an optional ALSA-seq
  auto-connect.
- libsamplerate when the host rate is not 48 kHz.
- A placeholder UI: plain rectangles for the keys, LED colours, and a CPU-load readout.
- **Done when:** a MIDI controller plays the instrument through PipeWire-JACK, and there are no
  xruns at a 64-frame buffer.
- As built:
  - `app/` builds `build/bin/champi` with `dpf_add_plugin(champi TARGETS jack UI_TYPE opengl)`.
    The JACK client is `CHAMPI`, with ports `mic`, `line_l`, `line_r`, `master_l/r`, `phones_l/r`,
    `events-in` and `midi-out`. Under PipeWire, DPF's jackbridge loads `pipewire-jack`'s libjack, so
    no JACK server is needed.
  - DPF's standalone `main()` is compiled as `dpf_jack_main` (`-Dmain=` on `DistrhoPluginMain.cpp`
    in the `champi-jack` target only), so `app/main.cpp` can run CHAMPI's options first: the
    `sd_cli` options (reset, import and export run and exit, as there's no script to run after them)
    and `--no-connect`. Anything else, such as DPF's `embed <id>`, goes on to DPF.
  - `ChampiPlugin` starts the `Runtime` with `AudioClock::kHost` in its constructor and stops it in
    its destructor; DPF builds the plugin once in the standalone. `run()` writes every JACK MIDI
    event to the virtual TRS port, runs the audio, and splits the firmware's MIDI out into events
    (`core/midi_splitter`: running status, real-time bytes; SysEx dropped, TAPE sends none).
  - `core/host_audio` (`champi_host`) maps the channels (mic, line L/R onto the firmware's inputs 1,
    3 and 4; master out first) and calls `ProcessAudio` directly at 48 kHz. At other rates it
    converts both ways with libsamplerate (`SRC_SINC_FASTEST`), with a small FIFO that only starts
    playing once the converters have filled it. It measures the load (the share of each cycle spent
    waiting on the firmware), late blocks (`ProcessAudio` timed out after audio had started) and
    resampler dropouts.
  - DPF's standalone doesn't connect ports or report xruns, so `app/jack_monitor` opens a second
    small client, `CHAMPI monitor`. It counts xruns and, unless `--no-connect` is given, connects
    master out to the first two physical playback ports and every physical MIDI source (except
    "Midi Through") to `events-in`. PipeWire lists ALSA sequencer devices as physical JACK MIDI
    ports, so this replaces the separate ALSA-seq auto-connect of the plan. Under a JACK2 server
    without a2jmidid, ALSA-only controllers need connecting by hand.
  - The UI goes through `DISTRHO_PLUGIN_WANT_DIRECT_ACCESS` for the load figures. Panel state and
    LEDs come from `Runtime::Get()` directly, since both are process singletons. The placeholder
    panel already plays by mouse (click keys, scroll and click encoders, toggle and line-in boxes),
    scaling the LEDs up by 4 and 11 so they show. It fits itself into whatever size the window
    manager gives it.
  - Checked with a JACK probe client at `PIPEWIRE_QUANTUM=64/48000`: MIDI note 60 plays C4 at
    261.60 Hz and notes 64, 65 and 67 land on E4, F4 and G4. There were no xruns, late blocks or
    dropouts in a 90 s run with notes, including during a 32-core parallel build. At 64/44100 (the
    resampled path) C4 measured 261.59 Hz, again with no xruns, late blocks or dropouts. The load
    reads about 3-4% of a 64-frame cycle.
  - With every core saturated (48 busy loops on 32 cores) there were still no xruns, but 2 late
    blocks: the firmware thread runs at normal priority and can be starved. Making it real-time
    would be a daisycola change (the thread is created there and is about 28% busy in steady
    state, mostly polling), so it's left for later.
  - Still manual: playing it from a real MIDI controller, which is the done-when.

### 6. Panel UI
- A layout extractor that reads the enclosure and main-board `.brd` files and writes
  `panel_layout.h`.
- A NanoVG vector panel styled after `interface.jpg`, with a fixed-aspect resizable window.
- LED rendering from the `LedFrame`: undo the firmware's scaling, apply gamma, add a glow.
- Mouse input: keys, encoder drag, scroll and push, the toggle switch, and the jack-detect
  toggle.
- An optional user `skin/` folder; no art is committed.
- **Done when:** the panel matches the reference side by side and is fully playable with the
  mouse.
- As built:
  - `tools/extract_panel_layout.py` (Python, standard library only) writes `app/panel_layout.h`,
    which is committed. The ctest `champi_panel_layout_up_to_date` reruns it with `--check`, so the
    header can't drift from the board files. Everything is in panel millimetres, origin top left,
    y down: the panel is 326 × 106 mm.
  - The top panel gives the outline, the key cut-outs, the shaft holes and the LED windows. The main
    board gives what each one is: `KEYn`, `SWn` (= ENCn; ENC4 is the leftmost, the speed knob) and
    `LED1`-`LED10`, whose left-to-right order is the PTH chain order in `led_frame.cpp`. The board
    sits at (+2.615, +2.9) mm under the panel, fitted from the five small encoder shafts to within
    0.02 mm. ENC5 and the jacks are on the lower board, at their own offset, fitted from ENC5 and
    the scrub wheel's 35.5 mm hole.
  - A key's centre is its switch's centre post (the footprint's 4.09 mm hole, mirrored by `MR0`),
    not the socket's origin, which is 3.81 mm off. That puts every key in the middle of its
    cut-out, and each SMT LED 5.05 mm straight above its key's centre.
  - There are ten LED windows, not six: 8.4 mm ones over ENC1-4, ENC6 and the CHOMPI key, and
    5.1 mm ones over play, loop and either side of the scrub wheel (ENC5's two LEDs).
  - `app/panel.{h,cpp}` (`champi_panel`, no DPF) holds the sizes the board files don't give (18 mm
    keycaps, knobs), hit-testing, the LED look and `MouseControl`. The UI only draws and forwards
    events.
  - LEDs: the firmware's /4 and /11 are undone, and the result is treated as linear light and
    gamma-encoded (1/2.2) for the screen. It's added onto the LED's unlit colour, with a radial
    glow. After boot TAPE lights KEY1, 8 and 15 a dim pink (`0f0509`), not the purple of the
    reference render; the panel shows what the firmware sends.
  - Mouse: keys play while held. Encoders turn with a vertical drag (2 mm a detent, up is
    clockwise) or the wheel (fractional smooth-scroll steps add up). A click pushes for 80 ms;
    holding still for 300 ms pushes until release, and a drag then turns it while pushed. The
    toggle and line-in jack flip on a click. The board files don't say which way the toggle is on;
    lever up is on here.
  - The line-in and headphone jacks are on the side of the case, so they're drawn at the right
    edge, level with the real ones. Clicking line in plugs or unplugs a cable.
  - The look follows the reference render (black body, cream and gold outlines, gold-ringed white
    knobs, purple scrub wheel, pink, cyan and yellow keys), with none of its art: the logo is
    "CHAMPI" in DejaVu Sans, the CHOMPI key has a mushroom, and play and loop have plain glyphs.
    The reference image isn't in the CHOMPI release; it was compared by eye from a local copy.
  - Skin: `logo.png`, `chompi.png`, `play.png` and `loop.png` in `~/.config/champi/skin` (or
    `$XDG_CONFIG_HOME`, or `--skin <dir>`), each optional. PNG only: NanoVG loads images through
    stb_image and has no SVG loader.
  - The window opens at 1141 × 392 (3.5 px/mm), keeps its aspect ratio and can shrink to half that.
    The status line moved under the panel. Tiling window managers still get a letterboxed panel.
  - Tests: `champi-panel-tests`, its own program because it attaches a `PanelState`, checks the
    layout against the board's key pitch and rows, hit-testing, the LED light, and every mouse
    gesture into a real `PanelState` on the manual clock. It passes under ASan and TSan.
  - Still manual: playing it by mouse on a real desktop. Screenshots of the running app were
    compared with the reference, but no clicks were injected.

### 7. Computer keyboard
- Scancode mapping, with defaults from the detailed plan's §4.
- `~/.config/champi/keymap.toml`.
- Encoder select, turn and push from the keyboard.
- **Done when:** everything on the panel can be played without the mouse.
- As built:
  - `app/keyboard.{h,cpp}` (in `champi_panel`, no DPF) holds the `Keymap` and `KeyboardControl`,
    alongside `MouseControl`. Keys are Linux evdev scancodes named as on a US keyboard; DPF's
    `keycode` on X11 is the scancode plus 8. DPF only has an X11 backend on Linux, so this holds
    under XWayland too.
  - The defaults are the detailed plan's §4, plus two things it left open. F1-F6 pick the encoders
    left to right as they sit on the panel (ENC4, 1, 2, 3, 5, 6), not by board number, so F1 is the
    speed knob. Line in, which §4 has no key for, is `F12`. ENC4 is selected at start.
  - `keymap.toml` is a small subset of TOML, parsed in-house: `action = "Key"`, a scancode number,
    or a list (`[]` unbinds). Actions are `key_1`-`key_25`, `chompi`, `play`, `loop`, `toggle`,
    `line_in`, `encoder_1`-`encoder_6`, `turn_left`, `turn_right` and `push`. It overrides the
    defaults action by action: an action it names loses its default keys, and a key it names leaves
    its old action. A typo, an unknown key or a key set twice is an error with the line number, and
    `champi` exits on it. `--keymap <file>` reads another file and `--print-keymap` writes the one
    in use, as a starting point. The skin and keymap now share `ConfigDir()`.
  - Panel keys play while held; two computer keys on one panel key hold it until both are up. The
    turn keys turn once, then repeat every 50 ms after 300 ms. The window's own key repeat is off
    (`setIgnoringKeyRepeat`), since X11 repeats as release-and-press pairs that would retrigger
    notes. The push key holds the encoder it pushed even if another is selected meanwhile, so
    push-and-turn works. Losing focus releases everything.
  - The knobs' pointers add up mouse and keyboard turns. A cream ring marks the selected encoder,
    but only once the keyboard has been used.
  - Tests: `champi-keyboard-tests`, its own program for its own `PanelState`. It checks the
    default keymap against the board (each black key between the white keys its computer keys
    flank, F1-F6 left to right), the TOML parsing and errors, the round trip through
    `--print-keymap`, and every key into a real `PanelState`. It passes under ASan and TSan.
  - Still manual: playing it from the keyboard on a real desktop. There was no Xvfb or xdotool to
    inject keys into the running app.

### 8. Fidelity and polish (milestone 5 of the detailed plan)
- Check the shift-menu flows, the looper, and recording through mic and line-in.
- Options persistence.
- Test mode (ENC6 held at boot).
- Simulated SD-card removal (`SdSetPresent`).
- Encoder feel and acceleration tuning.
- Write the full README.
- As built:
  - The flows are checked by golden tests in `champi-headless-tests`, one firmware run each:
    recording a sample from line in and from the mic, the looper (record, close, overdub, pause,
    resume), the shift menu's copy, erase, bank and mode changes (checked on the card's files),
    `options.json` (MIDI in and out channels, record latch, written back unchanged), test mode run
    to the end, and pulling the card. Each matched what TAPE's code says first time; none needed
    a CHAMPI or daisycola fix beyond the charger bit below.
  - The script language grew `input mic|line sine <hz> [<level>]` and `input ... off` (headless
    now feeds the inputs, kept 1024 frames ahead of the output), `sd out|in` and `midiloop on|off`
    (MIDI out back to MIDI in, as the factory jig does). The log gained `midiout` lines, whole
    messages split by `MidiSplitter`.
  - With the factory "Tape Slew", a looper pause is a tape stop: the loop slows down over about
    1.2 s and then holds its last sample, a small DC offset (about 0.005). A real CHOMPI's output
    capacitors would block that; CHAMPI passes it through. The test checks the AC level.
  - Test mode needed VIN_RDY (0x12 bit 5), which the MP2722 model now reports with USB power:
    the test's power-cable check waits for it to rise. The fault register 0x14 already read 0.
    `Runtime::Start` takes `test_mode`, which pushes ENC6 before the firmware starts;
    `champi --test-mode` uses it and the UI lets go once TAPE has booted. The whole factory test
    passes headless, including the 20 looped MIDI notes; the app needs `midi-out` connected to
    `events-in` for that check.
  - Options persistence needed no code: TAPE reads `options.json` at boot and writes it back.
    There's no options UI on the device either; the README shows the export, edit, import way.
  - `CardSlot` (`core/card_slot`, in `champi_board`) pulls the card through `SdSetPresent` and
    remembers whether it's in, since daisycola has no getter. `Runtime::Card()` owns it. TAPE
    blinks every LED red for 3 s, then plays only its built-in sample; putting the card back needs
    a restart, as on the device, and the status line says so.
  - The extractor now places the USB-C socket (J1) and the micro-SD holder (P5) from the main
    board: both are on the front edge, at x = 25.6 and 253.5 mm, their mouths just past the
    outline. They're drawn just inside it, under the key numbers. A click on the socket plugs or
    unplugs USB, the wheel over it sets the battery from 2.8 to 4.2 V (charge done at 4.2 V), and
    a click on the slot pulls or inserts the card. Keyboard: `sd_card` (F9) and `usb` (F10).
    TAPE shows a full battery for 20 minutes after charging stops, so the battery control mostly
    matters for the low-battery lockout.
  - Encoder feel: TAPE has no acceleration, and its speed, start and end knobs move 0.003 a
    detent (333 detents end to end), so the host accelerates. `TurnGain` is 1 up to 12 detents a
    second and grows with the rate up to 4; `TurnRate` smooths the rate over about 50 ms and
    starts again after a 150 ms pause. Drags and the wheel use it. Held turn keys go 20, 40 then
    80 detents a second (after 1.3 s and 2.3 s held). `QueueTurn` caps what's queued on an encoder
    at 12 detents, about 150 ms at daisycola's 80 a second, so a knob stops soon after the hand;
    turning back always gets through. The constants are a first guess from the arithmetic, not
    from feeling the hardware.
  - All 87 tests pass, and pass under ASan and TSan. TSan first caught a test holding ENC6 for a
    fixed 1.5 s: TAPE counts it over 5000 polls of 100 us, which TSan stretches. Scripts now hold it
    until `boot`, as the app does.
  - Screenshots of the running app (normal and `--test-mode`) were taken with niri and checked.
  - README rewritten in full: features, every option, the panel, keyboard, MIDI, a tour of TAPE
    (recording, looper, shift menu, presets and options, battery, test mode, pulling the card),
    the SD card, the whole script language and troubleshooting.
  - Still manual: playing it by hand to judge the encoder feel, and the CHOMPI output's DC
    blocking (left as is).

### 9. Connections menu
Routing CHAMPI's ports from inside the app, so they don't need connecting by hand in qpwgraph or
`pw-link` on every start. Today `JackMonitor` connects master out to the first two playback ports
and every hardware MIDI source to `events-in`, once, at start; the inputs, phones out and
`midi-out` are never connected, nothing is saved, and a controller plugged in later is missed.

- **Routing model** (`app/routing.{h,cpp}`, no JACK or DPF, so the tests can use it):
  - CHAMPI's nine ports: `mic`, `line_l`, `line_r` and `events-in` (inputs), `master_l`,
    `master_r`, `phones_l`, `phones_r` and `midi-out` (outputs), each with its type and direction.
  - The peer ports each one can connect to, grouped by client, with a readable name for each (the
    JACK pretty-name metadata or alias PipeWire sets, falling back to the port name).
  - The connections that exist and the ones wanted. Stereo pairs (master, phones, line in) are one
    row by default, connecting L to L and R to R (a mono peer gets both); a row can be split to
    route each side on its own.
  - Behind a small backend interface, so the tests drive it with a fake JACK.
- **Live JACK backend** (`JackMonitor` grows from a one-off connector into a service):
  - Port registration and connect callbacks only mark the graph changed: JACK functions can't be
    called from inside them. The monitor's own thread then lists ports and connections again
    (`jack_get_ports`, `jack_port_get_all_connections`) and publishes a snapshot to the UI under a
    mutex. The UI thread never calls JACK, which can block under PipeWire.
  - The menu's changes go to that thread as requests (`jack_connect`, `jack_disconnect`).
  - Connections made elsewhere (qpwgraph, `pw-link`) show up in the menu as they happen.
- **Saved routing** (`~/.config/champi/connections.toml`, the same small TOML subset as
  `keymap.toml`):
  - One line per CHAMPI port with the list of peers, e.g. `master_l = ["system:playback_1"]`.
  - Written when the menu changes a connection. Connections made outside the menu aren't saved,
    so a session in qpwgraph doesn't silently rewrite the file.
  - Restored at start once CHAMPI's ports exist, and again whenever a saved peer port appears, so
    a USB interface or controller plugged in later gets connected.
  - Peers are matched by port name, then by alias, since some PipeWire names (card numbers,
    `Midi-Bridge` ports) change between sessions. A saved peer that's missing is kept in the file
    and shown greyed in the menu.
  - Without a file, today's defaults apply (master to the first two playback ports, hardware MIDI
    sources to `events-in`). `--no-connect` still turns all of it off, saved routing included;
    `--connections <file>` reads another file.
- **The menu** (a NanoVG overlay in `champi_ui.cpp`, logic in `champi_panel` where it can be):
  - Opened and closed with `F8` only; `Esc` also closes it. Nothing is drawn on the panel or the
    status line to open it: no button, label or click target. F8 is a keymap action like the rest,
    `connections`, so `keymap.toml` can move it; its default is `F8`, which no panel action uses.
  - Drawn over the panel, which stays visible but dimmed. Two columns, "Inputs" (`mic`, line in,
    `events-in`) and "Outputs" (master, phones, `midi-out`); a row opens the list of peers that
    fit it, grouped by client, each with a checkbox. Scrolls when the list is long.
  - Mouse and keyboard: click to tick or untick; arrows move, `Enter` or `Space` ticks, `Esc`
    goes back a level.
  - While it's open, the panel takes no mouse or keyboard input; held panel keys are let go when
    it opens, as on losing focus.
  - Without a JACK server (DPF's native-audio fallback), F8 shows a one-line note that routing
    needs JACK or PipeWire, instead of the menu.
- **Done when:** every CHAMPI port can be connected and disconnected from the F8 menu without
  qpwgraph, the routing is restored on the next start and when a saved device is plugged in, and
  nothing on the rendered panel hints at the menu.
- Tests: the routing model against a fake backend (listing, pairing, matching by name and alias,
  restore and hotplug, the defaults without a file, `--no-connect`), the TOML round trip and its
  errors with line numbers, and the menu's navigation and hit-testing without DPF. ASan and TSan
  clean. Still manual: the real backend under PipeWire, including whether WirePlumber links or
  restores CHAMPI's ports on its own and fights the menu.
- How it went:
  - Under pipewire-jack the port names are already readable (the node descriptions, as in
    `MiniFuse 1 Main Output L/R:playback_FL`), and neither aliases nor pretty-names are set. A
    client whose name is taken gets a `-<number>` suffix (`MiniFuse 1 Loopback L/R-62`), which
    changes between sessions, so a saved peer is matched by name, then alias, then by name with
    that suffix dropped. The menu groups by client and shows the port part.
  - Some PipeWire ports carry odd flags (both input and output), so the backend lists each type
    and direction with `jack_get_ports` filters, as the old connector did, not from the flags.
  - The stereo pairing is by order within a client: consecutive ports pair up, an odd one left
    over is mono and gets both sides. The label reads `playback_FL/FR`.
  - Split isn't saved: a pair starts split if its connections only fit one side at a time, and
    the "route left and right separately" line at the top of a pair's list toggles it.
  - The defaults count as saved once applied: the first menu change writes them to the file with
    it. Until then a hardware MIDI source plugged in later is connected too.
  - Restoring is on appearance only (CHAMPI's ports, then each new peer), so a saved connection
    undone in qpwgraph isn't fought over.
  - `keymap.toml`'s parser moved to `app/toml_lines` and serves both files; it now knows `\"`
    and `\\` in strings. `MouseControl::Cancel` lets go of the mouse without a release's click
    when the menu opens.
  - Checked against PipeWire: the defaults, the restore at start, a saved port that appears later
    (a fake JACK client), the `-<number>` match, and the menu's requests through `JackMonitor`
    (a scratch harness, also under TSan: the only reports are inside libpipewire). The app under
    TSan reports only the GL driver's threads at window creation; under ASan it's clean.
  - Screenshots of both levels of the menu were taken with niri, from a scratch build that opens
    it at start (there's no key injection here).
  - 113 tests pass (25 new in `champi-routing-tests`), and the unit suites pass under ASan and
    TSan.
  - Still manual: pressing F8 and clicking through the menu by hand, and whether WirePlumber
    fights it.

### 10. Later: TEMPO and WAVE
- daisycola phase 6: TIM16 MIDI clock, `MidiManager` DMA transmit, `f_opendir`/`readdir`.
- Their card profiles and the libDaisy fork headers for each.
- Probably one chunk per firmware.

## Working agreement

- Firmware comes from an upstream submodule (`CHOMPI-Club/CHOMPI` @ `a73d732`) and is never patched.
- The public submodules (CHOMPI, DPF) use HTTPS URLs; daisycola uses SSH because it's private.
  DPF has no release tags, so its pin is a `main` commit, moved forward only in a chunk PR.
- The product name is `champi`: binaries `champi` and `champi-headless`, config in `~/.config/champi`, data in `~/.local/share/champi`. (The detailed plan first said `chompi-linux`.)
- The GitHub repo `pablo-penovi/CHAMPI` is private.
- The libDaisy replacement lives in `pablo-penovi/daisycola` (private), not here. Generic Daisy
  work goes there; CHOMPI-specific work stays here. CHAMPI moves its daisycola pin forward only
  in a chunk PR.
- Each chunk gets its own branch (`chunk-N-<slug>`) and a PR into `main`. You review and merge.
- After opening each PR I stop and summarise what changed and anything that deviated from the detailed plan.
