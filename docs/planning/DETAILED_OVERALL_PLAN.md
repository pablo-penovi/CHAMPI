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
- **Execution contexts.** On the device these preempt each other on one core, highest priority first:
  - `AudioCallback` (`chompi_main.cpp:60`): 48 kHz, 24-frame blocks, 4 in / 4 out. Inputs: mic, unused, aux L, aux R. Outputs: headphones L/R, master L/R. It also polls the controls every block.
  - `SDCallback`: TIM4 at 1 kHz, does all FatFS I/O.
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

### Layout (new tree, firmware tree untouched)
```
ports/linux/
  CMakeLists.txt            top level; DPF via git submodule ports/linux/third_party/DPF
  host-daisy/               libDaisy hardware shim (include path placed BEFORE vendored libDaisy/src)
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
The firmware sources are compiled straight from `firmware/chompi-tape/code/src`. Each snag is handled on the shim side so the firmware tree needs no patches:
- A forwarding `Limiter.h` fixes the include case.
- `DSY_SDRAM_BSS` and `DMA_BUFFER_MEM_SECTION` are defined empty.
- At startup the shim `mmap`s 64 MB with `MAP_FIXED_NOREPLACE` at `0xC0000000`. Raw-address uses (`ZeroSDRAM`, and TEMPO's `SampleManager` later) then work unchanged.
- The firmware's `main` is renamed per translation unit with `-Dmain=chompi_fw_main`.

The host build uses gnu++17 and gcc. Clang support is optional because the firmware uses `__attribute__((optimize("-O0")))`.

### 1. host-daisy shim: same API as the vendored libDaisy, emulated at the pin/peripheral level
- **System**: `GetNow`/`GetUs`/`GetTick` from `steady_clock`, `Delay`/`DelayUs` sleep, and reset becomes a re-exec of the process.
- **GPIO / `dsy_gpio`**: a table of virtual pins. Input pins read from `PanelState`:
  - Encoder 5/6 A/B and the encoder 5 click.
  - Jack-detect.
  - `mpc_int` reads "no interrupt".
- **ShiftRegister4021<N,1>**: a reimplementation of the vendored fork's logic (debounce with `dbc_size`, `State`, `RawState`, edges). Its "serial data" comes from `PanelState` key bits and encoder 1–4 A/B bits.
- **Encoders**: a virtual EC12 quadrature generator. Each pending detent advances the A/B Gray code by one state per control poll (about 2 kHz). The firmware's own `ChompiEncoder::Debounce` decodes it, so acceleration and debounce behave as on the hardware.
- **I2C (MP2722)**: a register model answering `TransmitBlocking`/`ReceiveDma`. It reports VIN good, no legacy cable, battery not low and charge done, and invokes `batteryCallback` synchronously. The battery-lockout loops then exit, and holding ENC6 shows a battery level that can be set from the UI.
- **TimerHandle**: TIM4 (1 kHz) and later TIM16 become timer contexts run by the virtual MCU (see §3). `SetPeriod` and `SetPrescaler` are honoured.
- **TimChannel `StartDma`**: models the WS2812 transfer. After the real transfer time (bits × 1.25 µs) it decodes the PWM duty buffer back into RGB, applying GRB/RGB order and dropping porch slots. It publishes an `LedFrame`, then fires the completion callback in interrupt context, so the `EndOfLeds` ping-pong runs exactly as written.
- **Audio** (`AudioHandle`/`SaiHandle`/`DaisySeed::StartAudio`): stores the callback. Sample rate 48000, block size 24, 4×4 channels.
- **MIDI**: `MidiUartHandler` is fed from the host MIDI input queue, with the real `MidiParser` producing `MidiEvent`s, and its sends go to the host MIDI output. `MidiUsbHandler` is reported as not connected, so outgoing MIDI is not doubled. `ResetTransport` is a no-op.
- **SdmmcHandler / FatFSInterface / diskio**: the vendored `Middlewares/Third_Party/FatFs/src/ff.c` with `src/sys/ffconf.h`, and a host `diskio` over a FAT32 **image file**, which gives full FatFS fidelity. `disk_status` reports "card present"; a UI action can simulate removing the card.
- **CMSIS/HAL stubs**: `__disable_irq`/`__enable_irq`/`__get_PRIMASK` map onto the virtual-MCU interrupt mask. `HAL_PWR_EnterSTOPMode` and `SCB_CleanDCache_by_Addr` are no-ops.

### 2. Virtual SD card
- The image lives at `~/.local/share/chompi-linux/sdcard.img`.
- On first run it is created, sparse at 4 GB, using FatFS's own `f_mkfs`, and seeded from `firmware/card-profiles/tape-2.0/` (samples, `options.json`, `presets.json`).
- CLI subcommands: `--sd-import <dir>`, `--sd-export <dir>` and `--sd-reset`. Users can also loop-mount the image or use mtools.
- At boot, the firmware's own `FileCopier::NeedsOverwrite` normalises headers and `_double` files exactly as on the device.

### 3. Virtual MCU runtime (preserves single-core preemption semantics)
The firmware assumes a strict priority order: audio ISR > TIM4 ISR > main loop. Its FIFOs are not thread-safe, and the code has comments like `Delay(5) // fixes data race`. So we emulate one core instead of running free threads:
- **MCU thread.** One dedicated thread runs `chompi_fw_main()`, which never returns.
- **Interrupts.** They are delivered to the MCU thread as POSIX real-time signals (`pthread_sigqueue`) and run on its stack, just as an IRQ preempts code on the STM32:
  - **Audio signal.** It has the highest priority, so it blocks every other interrupt signal. The handler runs `AudioCallback` for one 24-frame block, reading and writing lock-free SPSC ring buffers shared with the host audio thread.
  - **TIM4 signal.** It is driven by a 1 kHz `timer_create` (`SIGEV_THREAD_ID`). The audio signal is left unmasked during this handler so audio can preempt SD work.
  - **LED-DMA completion signals.**
- **IRQ blocking.** `__disable_irq` and `ScopedIrqBlocker` become `pthread_sigmask`.
- **Async-signal safety.** The ISR bodies are DSP code, `ff.c` and our `pread`/`pwrite` diskio, which are all safe here. Startup asserts that the firmware's ISRs make no malloc or stdio calls.
- **Audio adapter** (host audio thread, i.e. DPF `run()`):
  - Pushes input frames, raises the audio signal for each 24 frames that are ready, and pulls output.
  - Uses a fixed 2-block (1 ms) safety buffer.
  - If the host rate is not 48 kHz, wraps this in libsamplerate. The default is to ask PipeWire/JACK for 48 kHz.
- **Load.** The emulated "CPU load" is measured per block for a debug overlay.
- **Shutdown.** Wait for no pending FatFS write (presets are written via a temp file and rename), then `quick_exit`.

### 4. DPF app (`app/`)
- **Plugin side** (`ChompiPlugin`):
  - Audio: 3 inputs (mic, line L, line R) and 4 outputs (master L/R, phones L/R). By default master goes to the system playback ports.
  - MIDI in and out are enabled. JACK MIDI accepts any number of controller connections, which merge into the virtual TRS input. A built-in ALSA-sequencer auto-connect option covers non-JACK use.
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
  - Keyboard: physical scancodes, so the mapping does not depend on layout, configurable in `~/.config/chompi-linux/keymap.toml`. Defaults, using tracker-style two-octave musical typing:
    - White keys 1–7 = `Z X C V B N M`, white keys 8–15 = `Q W E R T Y U I`.
    - Black keys = `S D G H J` and `2 3 5 6 7`.
    - CHOMPI key = `Tab`, Play = `Space`, Loop = `Enter`, toggle switch = `` ` ``.
    - `F1`–`F6` select an encoder; `←`/`→` (or `[`/`]`) turn it; `\` pushes it.
  - MIDI controllers work natively through the firmware's own MIDI handling: notes play keys, CC20–25 set encoders, CC26/27 work play/loop, on the channel set in `options.json`.

### 5. Build and dependencies
- CMake ≥ 3.20.
- DPF (submodule) with `dpf_add_plugin(... TARGETS jack)`. DPF's standalone falls back to native ALSA/PulseAudio/SDL when JACK is absent; PipeWire-JACK is present on this machine.
- Libraries: libsamplerate and alsa (seq).
- Targets: `chompi-linux` (app), `chompi-headless`, `chompi-tests`.
- A `CHOMPI_FIRMWARE=tape|tempo|wave` option selects which firmware sources to compile and which shim variant to use.

### 6. Milestones
1. **Shim + headless.** TAPE compiles and links. `chompi-headless` boots against an SD image, runs the boot animation (LED log), plays a scripted key press, and writes a WAV.
2. **Virtual SD.** Image creation, seeding, import and export.
3. **Virtual MCU.** Signal-based interrupts, the audio adapter, LED DMA emulation, and a ThreadSanitizer/ASan run that comes up clean.
4. **DPF app.** Audio and MIDI I/O, the panel UI with live LEDs, mouse and keyboard input.
5. **Fidelity and polish.** Encoder feel and acceleration, the shift-menu flows (bank/mode, save/copy/erase presets), looper and record via mic and line-in, options persistence, test mode (ENC6 held at boot), simulated SD removal, keymap config, README.
6. **Later: TEMPO and WAVE.** Shim superset: TIM16 MIDI clock, `MidiManager`'s `GetUartHandle`/`DmaTransmit`, `f_opendir`/`readdir`, `GetUs`. Use their card profiles and the tempo/wave libDaisy fork headers.

## Verification
- **Unit tests** (`chompi-tests`):
  - The quadrature generator, decoded through the firmware's real `ChompiEncoder`.
  - The 4021 debounce model.
  - WS2812 encode→decode round trip using the firmware's real `fill_led_data`.
  - FatFS image mkfs, seed and read back.
  - The MP2722 model keeps `LowBatteryLockoutCheck` non-blocking.
- **Headless golden tests:**
  - Script: boot, press KEY1 with the factory card, record 2 s, then check the output is non-silent and the pitch is right for slot 1.
  - Turn encoder 0 and check the pitch changes.
  - Press play, loop, record and check the looper output.
  - Write a preset in the shift menu, restart, and check `presets.json` changed in the image.
- **Sanitizers:** run the headless suite under `-fsanitize=thread` and `address`.
- **Manual:**
  - Run `chompi-linux`. Check the panel matches the reference and the boot rainbow plays.
  - Play from the keyboard and mouse.
  - Connect two MIDI controllers in qpwgraph/aconnect: notes play, CC20–27 move the virtual encoders (LEDs follow), and MIDI out reaches a soft synth.
  - Record from the mic.
  - Check the CPU overlay shows no xruns at a 64-frame JACK buffer.
- **Hardware comparison** (if a real CHOMPI is available): play the same MIDI file into both and compare recordings and LED behaviour.
