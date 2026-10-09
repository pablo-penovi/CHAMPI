# Plan: native Linux virtual CHOMPI (TAPE first)

## Context

The repo is the discontinued, MIT-licensed CHOMPI release: hardware files plus three firmwares (TAPE sampler, TEMPO, WAVE) for a Daisy Seed (STM32H750, libDaisy/DaisySP). The goal is a native Linux C++ app that recreates the instrument as faithfully as possible:
- an on-screen panel styled closely after `interface.jpg` (for personal use; it is Temecula DSP's NIBBI art, and the CHOMPI name and art are trademarks, see `TRADEMARKS.md`);
- played from the computer keyboard, the mouse, and any number of MIDI controllers.

Decisions so far:
- Start with **TAPE** and keep the design open for TEMPO and WAVE.
- Get fidelity by **compiling the real firmware sources unmodified** on x86 against a host replacement for libDaisy's hardware layer.
- Use **DPF** (DISTRHO Plugin Framework) for the app shell. It gives a JACK/native-audio standalone app now and LV2/VST3/CLAP builds later from the same code.

Key facts from exploration (TAPE, `firmware/chompi-tape/code/src`):
- **The only hardware boundary is `chompi::Hardware`** (`hardware.h`) plus `temp_led_stuff.h`. Everything else is portable C++ on top of libDaisy APIs.
- **Execution contexts.** On the device these share one core. The audio DMA interrupt preempts the
  TIM4 callback, which preempts the main loop:
  - `AudioCallback` (`chompi_main.cpp:60`): 48 kHz, 24-frame blocks, 4 in / 4 out. Inputs: mic, unused, aux L, aux R. Outputs: headphones L/R, master L/R. It also polls the controls every block.
  - `SDCallback`: TIM4, does all FatFS I/O. TAPE asks for 1 kHz, but libDaisy truncates the
    period to 16 bits, so it really runs at about 4.4 kHz, on the device too.
  - `MainLoop` in `while(1)` (`:179`): UI events, MIDI, preset flush, battery.
- **Controls:**
  - 28 keys and the encoder clicks are read through 5 chained CD4021 shift registers (`SwId`, `hardware.h:46`).
  - 6 encoders. Encoders 1–4 have their A/B lines on a 6th CD4021; encoders 5 and 6 use GPIOs (D0/D20 with click on D10, and D15/D17).
  - Toggle switch `SW_TOG`, and a line-in jack-detect on D21.
  - Key roles: KEY1–15 are the white row, KEY16–25 the black row, KEY26 the CHOMPI/shift key, KEY27/28 play and loop.
- **LEDs:** two WS2812 chains driven by timer PWM + DMA, ping-ponged by `EndOfLeds`. The PTH chain has 10 LEDs and the SMT chain has 25 key LEDs, each with 6 porch slots at both ends.
- **MIDI** (TRS UART + USB):
  - Notes in are mapped through `midi2key` (`ui.h`). CC20–25 set encoders, CC26/27 act as play/loop.
  - Notes and CCs are sent out.
  - There is no clock sync in TAPE.
- **SD card:** `{jammi|cubbi}_{a-e}{n}[_double].wav`, `options.json` and `presets.json`. Factory content is in `firmware/card-profiles/tape-2.0/`.
  - The code pokes FatFS internals (`FIL::obj.objsize`, and `f_size` used as an "is open" test). So we use the **real `ff.c`** rather than fake `f_*` calls.
- **Portability snags:**
  - `#include "Limiter.h"` vs the file `limiter.h`.
  - `ZeroSDRAM()` writes to `0xC0000000`.
  - `HAL_PWR_EnterSTOPMode`.
  - Battery-charger busy-waits over I2C.
  - The vendored libDaisy is a **fork** (`ShiftRegister4021` with `dbc_size`, `RawState` and edge accessors; `ResetTransport`), so the shim must match the vendored headers, not upstream.
  - DaisySP, coreJSON, FatFS and libDaisy's `ui/`, `util/FIFO`, `hid/midi_parser` and `MidiEvent` all compile on the host as they are.

## Approach

> **Update (2026-10-08):** the libDaisy shim (`host-daisy/`, §1) and the virtual MCU runtime
> (§3) have moved to their own repo, [daisycola](https://github.com/pablo-penovi/daisycola), used
> here as a submodule. daisycola phases 1–5 are done (`e9ab6a0`): TAPE links, boots headless and
> runs clean under TSan and ASan. §1–§3 below have been corrected to match what was built; the
> full reasoning is in daisycola's `docs/design-notes.md`, which wins where the two disagree.
> CHOMPI-specific pieces stay in CHAMPI: the panel wiring (`PanelState`), the MP2722 model and the
> LED layout (`LedFrame`). The repo layout, chunk order and progress are in
> [OVERALL_PLAN.md](OVERALL_PLAN.md). Names follow the product name `champi` (the plan first
> said `chompi-linux`).

### Layout (new tree, firmware tree untouched)
```
ports/linux/
  CMakeLists.txt            top level; DPF via git submodule ports/linux/third_party/DPF
  host-daisy/               [now the daisycola submodule] libDaisy hardware shim (include path placed BEFORE vendored libDaisy/src)
    include/daisy.h, daisy_seed.h, daisy_core.h, sys/system.h, per/{gpio,i2c,tim,sai,uart,sdmmc}.h,
            dev/sr_4021.h, hid/{switch,audio,midi,usb_midi}.h, util/scopedirqblocker.h,
            fatfs.h, stm32h7xx_hal.h (stubs), Limiter.h (-> #include "limiter.h")
    src/  virtual_mcu.cpp, sdcard_image.cpp (FatFS diskio), led_dma.cpp, ...
  core/                     "virtual hardware" library, independent of DPF
    panel_state.h           atomics: key bits, encoder pending detents, clicks, toggle, jack-detect
    led_frame.h             triple-buffered decoded LED RGB (35 LEDs)
    firmware_tape.cpp       compiles chompi_main.cpp (with -Dmain=chompi_fw_main), encoder.cpp,
                            FileStreamingManager.cpp, core_json.c, DaisySP, libDaisy ui/ + util/
    runtime.h/.cpp          start/stop, audio block adapter, MIDI queues
  app/                      DPF plugin + NanoVG UI (standalone "jack" target only for now)
  tools/chompi-headless.cpp scripted runner for tests (input script -> output WAV + LED log)
  tests/
```
This was the original layout inside the CHOMPI tree; CHAMPI's actual layout is in
[OVERALL_PLAN.md](OVERALL_PLAN.md). daisycola replaces far fewer headers than listed above: most
libDaisy headers only declare things and are used as they are, with daisycola supplying the
bodies (its `docs/headers.md` has the list).

The firmware sources are compiled straight from `firmware/chompi-tape/code/src`. Each snag is handled on the shim side so the firmware tree needs no patches:
- A forwarding `Limiter.h` fixes the include case.
- More turned up while building daisycola, all fixed there: FatFs's `DWORD` is 64-bit on x86-64
  (a force-included `ff_integer.h`), TAPE calls `f_write` with a null byte counter (a wrapper),
  and `DelayLine::SetDelay(10u)` is ambiguous on x86-64 (an overload, so daisycola's include
  directories must come before DaisySP's).
- `DSY_SDRAM_BSS` and `DMA_BUFFER_MEM_SECTION` are defined empty.
- At startup the shim `mmap`s 64 MB with `MAP_FIXED_NOREPLACE` at `0xC0000000`. Raw-address uses (`ZeroSDRAM`, and TEMPO's `SampleManager` later) then work unchanged.
- The firmware's `main` is renamed per translation unit with `-Dmain=chompi_fw_main`.

The host build uses gnu++17 and gcc. Clang support is optional because the firmware uses `__attribute__((optimize("-O0")))`.

### 1. host-daisy shim: same API as the vendored libDaisy, emulated at the pin/peripheral level
- **System**: `GetNow`/`GetUs`/`GetTick` from the monotonic clock, `Delay`/`DelayUs` sleep with `clock_nanosleep`. Reset isn't modelled: no CHOMPI firmware resets itself.
- **GPIO / `dsy_gpio`**: a table of virtual pins. Input pins read from `PanelState`:
  - Encoder 5/6 A/B and the encoder 5 click.
  - Jack-detect.
  - `mpc_int` reads "no interrupt".
- **ShiftRegister4021<N,1>**: the fork's `dev/sr_4021.h` runs unchanged. daisycola models the CD4021 chips at the pin level (clock, latch, data through `dsy_gpio`), so the fork's own debounce and edge code runs as written. CHAMPI attaches the chains with `AttachSr4021` and sets their inputs from `PanelState` key bits and encoder 1–4 A/B bits.
- **Encoders**: a virtual EC12 quadrature generator. Each pending detent advances the A/B Gray code one state every 3 ms (a dwell time, not one state per poll: `ChompiEncoder` samples at most once per ms and would skip states). The firmware's own `ChompiEncoder::Debounce` decodes it, so acceleration and debounce behave as on the hardware.
- **I2C (MP2722)**: a CHAMPI register model, registered with daisycola at address 0x3F (TAPE passes `0x3F | 0x80`; only the low seven bits reach the bus). It answers `TransmitBlocking`/`ReceiveDma` and reports VIN good (register 0x12, bit 6), no legacy cable, battery not low and charge done. `ReceiveDma` completes as an I2C interrupt after the transfer time, as on the chip, and runs `batteryCallback` there. `mpc_int` (D31) is held high. The battery-lockout loops then exit, and holding ENC6 shows a battery level that can be set from the UI.
- **TimerHandle**: TIM4 and later TIM16 become timer interrupts run by the virtual MCU (see §3). Rates come from the prescaler and period the firmware writes, at 2 × PCLK1, with the 16-bit truncation of TIM3/TIM4, so TIM4 runs at about 4.4 kHz as on the device.
- **TimChannel `StartDma`**: models the WS2812 transfer. daisycola captures the PWM duty buffer as a `DmaFrame` and, after the real transfer time, fires the completion callback in interrupt context, so the `EndOfLeds` ping-pong runs exactly as written. CHAMPI reads the newest frame with `GetDmaFrame` and decodes it with `Ws2812Decode`: a pulse longer than half the bit period is a 1, zero-duty porch words hold the line low, so it returns exactly 25 SMT (GRB) and 10 PTH (RGB) LEDs. CHAMPI maps those into its `LedFrame`.
- **Audio** (`AudioHandle`/`SaiHandle`/`DaisySeed::StartAudio`): stores the callback. Sample rate 48000, block size 24, 4×4 channels.
- **MIDI**: host bytes (`WriteMidiIn`) raise the UART receive interrupt, and the fork's own `MidiHandler` and parser produce `MidiEvent`s, as on the device. Sends go to an output ring the host drains (`ReadMidiOut`). USB is unplugged by default, so outgoing MIDI is not doubled. `ResetTransport` is a no-op.
- **SdmmcHandler / FatFSInterface / diskio**: the vendored `Middlewares/Third_Party/FatFs/src/ff.c` with `src/sys/ffconf.h`, and a host `diskio` over a FAT32 **image file** (MBR plus one partition, like a real card), which gives full FatFS fidelity. `SdSetPresent(false)` simulates removing the card.
- **CMSIS/HAL stubs**: `__disable_irq`/`__enable_irq`/`__get_PRIMASK` map onto the virtual-MCU interrupt mask. `HAL_PWR_EnterSTOPMode` masks every interrupt and waits for the host's `Wake()`. Cache maintenance is a no-op.
- **Missing APIs** fail at link time rather than silently doing nothing.

### 2. Virtual SD card
- The image lives at `~/.local/share/champi/sdcard.img`.
- On first run it is created, sparse at 4 GB, using FatFS's own `f_mkfs`, and seeded from `firmware/card-profiles/tape-2.0/` (samples, `options.json`, `presets.json`).
- CLI subcommands: `--sd-import <dir>`, `--sd-export <dir>` and `--sd-reset`. Users can also loop-mount the image or use mtools.
- At boot, the firmware's own `FileCopier::NeedsOverwrite` normalises headers and `_double` files exactly as on the device.

### 3. Virtual MCU runtime (preserves single-core preemption semantics)
The firmware assumes interrupts preempt the main loop in a fixed priority order. Its FIFOs are not thread-safe, and the code has comments like `Delay(5) // fixes data race`. So we emulate one core instead of running free threads (built in daisycola):
- **MCU thread.** One dedicated thread (16 MB stack) runs `chompi_fw_main()`, which never returns.
- **Interrupts.** They are delivered to the MCU thread as POSIX real-time signals (`SIGRTMIN + 2 + line`) and run on its stack, just as an IRQ preempts code on the STM32. Priorities follow libDaisy's NVIC settings:
  - **Priority 0:** audio DMA, I2C, UART, LED DMA and USB. They mask each other, so none preempts another; pending ones run in line order, audio first.
  - **Priority 15:** the timers, TIM4 included. Every priority-0 interrupt can preempt the TIM4 callback.
  - Timer interrupts come from one POSIX timer per line (`SIGEV_THREAD_ID`); host threads raise lines with `pthread_sigqueue`.
- **IRQ blocking.** PRIMASK, NVIC enables and STOP mode are kept as state, and `__disable_irq`, `ScopedIrqBlocker` and `HAL_NVIC_*` recompute the thread's signal mask from it. `__enable_irq` inside a handler returns to the handler's level.
- **Async-signal safety.** The ISR bodies are DSP code, `ff.c` and the `pread`/`pwrite` diskio, which are all safe here. Handlers save and restore `errno`. The planned startup assertion against malloc and stdio in ISRs wasn't built; the TSan and ASan runs came up clean without it.
- **No fallback needed.** The planned single-lock "interrupt runner" fallback was never built.
- **Sanitizers.** Under TSan, interrupts don't nest and arrive a little late (TSan defers signals). Under ASan, the raw SDRAM at `0xC0000000` sits in the shadow gap: the executable must set `protect_shadow_gap=0`.
- **Audio adapter** (host audio thread, i.e. DPF `run()`, calling daisycola's `ProcessAudio`):
  - Pushes input frames, raises the audio interrupt for each 24 frames that are ready, and waits (bounded) for the output.
  - The output ring starts with 2 blocks (1 ms) of silence as a safety buffer.
  - Samples go through libDaisy's own 24-bit conversions both ways, so they clip at ±0.999985 and full scale doesn't wrap.
  - Headless runs use daisycola's internal clock instead, a timer at the block rate.
  - If the host rate is not 48 kHz, wraps this in libsamplerate. The default is to ask PipeWire/JACK for 48 kHz.
- **Load.** The emulated "CPU load" is measured per block for a debug overlay.
- **Shutdown.** Wait for no pending FatFS write (`SdBusy`; presets are written via a temp file and rename), then `Halt` parks the firmware thread at its next delay outside a handler, and the process exits. A halted firmware can't restart, so each process runs the firmware once.

### 4. DPF app (`app/`)
- **Plugin side** (`ChompiPlugin`):
  - Audio: 3 inputs (mic, line L, line R) and 4 outputs (master L/R, phones L/R). By default master goes to the system playback ports.
  - MIDI in and out are enabled. JACK MIDI accepts any number of controller connections, which merge into the virtual TRS input. A built-in ALSA-sequencer auto-connect option covers non-JACK use.
  - Routing: a connections menu (`F8`) connects any of CHAMPI's ports to other JACK/PipeWire ports. A second JACK client lists the graph, follows changes made elsewhere, and saves the menu's routing to `~/.config/champi/connections.toml`, which is restored at start and when a saved device appears. Without that file, the defaults above apply.
  - The firmware runtime is a process singleton. That is fine for standalone; a future plugin would need it confined to one instance per process.
  - DPF parameters are not used for controls. The UI talks to the core through direct access (`DISTRHO_PLUGIN_WANT_DIRECT_ACCESS`), using `PanelState` and `LedFrame`.
- **UI side** (NanoVG, about 60 fps):
  - A vector-drawn panel that copies the reference image's look: black body; cream line art; gold-ringed encoders with white knobs; the big purple scrub encoder (ENC5); pink CHOMPI key, cyan play and yellow loop keys; 15 white and 10 dark keys with LED slots; the 6 round PTH LED windows; the toggle switch; mic and headphone glyphs; key numbers.
  - Control positions come from the real top panel geometry in `hardware/hardware-enclosure/CC_Chompi_Rev4_Enc_TOP.brd` (EAGLE XML holes, about 325×105 mm) and the main board `.brd` element positions, extracted once by a small script into `panel_layout.h`.
  - The window is resizable at a fixed aspect ratio.
  - An optional `skin/` folder can hold user-supplied PNG/SVG art to replace the character and logo glyphs. That art is not committed.
  - LEDs are rendered from the decoded `LedFrame`: undo the firmware's /11 and /4 scaling, apply gamma, and add a glow.
- **Input:**
  - Mouse: press and release keys; drag or scroll to turn an encoder; click an encoder to push it (hold works).
  - Line-in jack: clicking it toggles jack-detect (mic or line).
  - Keyboard: physical scancodes, so the mapping does not depend on layout, configurable in `~/.config/champi/keymap.toml`. Defaults, using tracker-style two-octave musical typing:
    - White keys 1–7 = `Z X C V B N M`, white keys 8–15 = `Q W E R T Y U I`.
    - Black keys = `S D G H J` and `2 3 5 6 7`.
    - CHOMPI key = `Tab`, Play = `Space`, Loop = `Enter`, toggle switch = `` ` ``.
    - `F1`–`F6` select an encoder; `←`/`→` (or `[`/`]`) turn it; `\` pushes it.
    - `F8` opens and closes the connections menu, an overlay drawn over the panel. Nothing on the rendered panel opens it or hints at it.
  - MIDI controllers work natively through the firmware's own MIDI handling: notes play keys, CC20–25 set encoders, CC26/27 work play/loop, on the channel set in `options.json`.

### 5. Build and dependencies
- CMake ≥ 3.20.
- DPF (submodule, pinned to a `main` commit because DPF has no release tags) with `dpf_add_plugin(... TARGETS jack)`. DPF's standalone falls back to native ALSA/PulseAudio/SDL when JACK is absent; PipeWire-JACK is present on this machine.
- Libraries: libsamplerate and alsa (seq).
- Targets: `champi` (app), `champi-headless`, `champi-tests`.
- A `CHOMPI_FIRMWARE=tape|tempo|wave` option selects which firmware sources to compile and which shim variant to use.

### 6. Milestones
Milestones 1–3 are done on the daisycola side; the CHAMPI side is chunks 1–4 of
[OVERALL_PLAN.md](OVERALL_PLAN.md).

1. **Shim + headless.** TAPE compiles and links. `champi-headless` boots against an SD image, runs the boot animation (LED log), plays a scripted key press, and writes a WAV.
2. **Virtual SD.** Image creation, seeding, import and export.
3. **Virtual MCU.** Signal-based interrupts, the audio adapter, LED DMA emulation, and a ThreadSanitizer/ASan run that comes up clean.
4. **DPF app.** Audio and MIDI I/O, the panel UI with live LEDs, mouse and keyboard input.
5. **Fidelity and polish.** Encoder feel and acceleration, the shift-menu flows (bank/mode, save/copy/erase presets), looper and record via mic and line-in, options persistence, test mode (ENC6 held at boot), simulated SD removal, keymap config, README.
6. **Connections menu.** Route every CHAMPI input and output to other JACK/PipeWire ports from the `F8` overlay, save the routing, and restore it at start and on hotplug.
7. **Later: TEMPO and WAVE.** daisycola phase 6: shim superset: TIM16 MIDI clock, `MidiManager`'s `GetUartHandle`/`DmaTransmit`, `f_opendir`/`readdir`, `GetUs`. Use their card profiles and the tempo/wave libDaisy fork headers.

## Verification
- **Unit tests.** daisycola already covers the quadrature generator through the firmware's real `ChompiEncoder`, the 4021 debounce, WS2812 decoding of the firmware's real `fill_led_data`, and FatFS mkfs, seeding with the TAPE card and read-back. `champi-tests` covers the CHOMPI board model:
  - Key presses and encoder turns through `PanelState` and `SwId`.
  - Decoded LEDs landing in the right `LedFrame` slots.
  - Seeding the card image from `card-profiles/tape-2.0/`.
  - The MP2722 model keeps `LowBatteryLockoutCheck` non-blocking.
- **Headless golden tests:**
  - Script: boot, press KEY1 with the factory card, record 2 s, then check the output is non-silent and the pitch is right for slot 1.
  - Turn encoder 0 and check the pitch changes.
  - Press play, loop, record and check the looper output.
  - Write a preset in the shift menu, restart (a second process on the same image), and check `presets.json` changed in the image.
- **Sanitizers:** run the headless suite under `-fsanitize=thread` and `address` (the ASan build sets `protect_shadow_gap=0`). CHAMPI must set the flags for its own targets as well as passing them to daisycola, because daisycola's flags only cover its own directory.
- **Manual:**
  - Run `champi`. Check the panel matches the reference and the boot rainbow plays.
  - Play from the keyboard and mouse.
  - Connect two MIDI controllers in qpwgraph/aconnect: notes play, CC20–27 move the virtual encoders (LEDs follow), and MIDI out reaches a soft synth.
  - Record from the mic.
  - Check the CPU overlay shows no xruns at a 64-frame JACK buffer.
- **Hardware comparison** (if a real CHOMPI is available): play the same MIDI file into both and compare recordings and LED behaviour.
