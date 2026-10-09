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
turned up the audio bug below. Phase 6 (TEMPO and WAVE) is for later, with chunk 9.

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
| ☐ | 6 | The real panel: vector drawing, layout taken from the `.brd` files, LED rendering, mouse control | It looks like the reference image and plays fully by mouse |
| ☐ | 7 | Computer keyboard control with a configurable keymap | It plays fully without the mouse |
| ☐ | 8 | Polish: shift menus, looper and recording, test mode, removing the SD card, encoder feel, README | Everything in the detailed plan's milestone 5 is covered |
| ☐ | 9 | Later: TEMPO and WAVE [daisycola 6] | |

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

### 8. Fidelity and polish (milestone 5 of the detailed plan)
- Check the shift-menu flows, the looper, and recording through mic and line-in.
- Options persistence.
- Test mode (ENC6 held at boot).
- Simulated SD-card removal (`SdSetPresent`).
- Encoder feel and acceleration tuning.
- Write the full README.

### 9. Later: TEMPO and WAVE
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
