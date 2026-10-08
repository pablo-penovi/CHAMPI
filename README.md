# CHAMPI

CHAMPI is a native Linux virtual instrument that runs the real
[CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) TAPE firmware on x86.

The CHOMPI is a discontinued, MIT-licensed sampler built on a Daisy Seed (STM32H750,
libDaisy/DaisySP). CHAMPI doesn't rewrite the firmware. It compiles the original firmware sources,
unchanged, against a host version of libDaisy's hardware layer. The firmware then runs on a
"virtual chip" that gives it emulated keys, encoders, LEDs, an SD card, audio and MIDI.

## Status

Planning stage: no code yet. The work is split into chunks, each ending in something that builds
and has tests. Progress is tracked in [docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md).

## Planned features

- An on-screen panel styled after the original instrument, drawn as vectors with live LEDs.
- Play it with the mouse, the computer keyboard (configurable keymap), or any number of MIDI
  controllers.
- A standalone JACK app (also works under PipeWire), built with
  [DPF](https://github.com/DISTRHO/DPF). LV2/VST3/CLAP plugins can come later from the same code.
- A virtual SD card stored as a disk-image file, seeded with the factory TAPE card content, with
  import and export.
- `champi-headless`, a scripted runner that takes an input script and writes a WAV file and an
  LED log, used for tests.
- TAPE first. TEMPO and WAVE may follow.

## Planned layout

```
third_party/CHOMPI/  submodule: CHOMPI firmware, card profiles and board files (never patched)
third_party/DPF/     submodule: DISTRHO Plugin Framework
host-daisy/          host replacement for libDaisy's hardware layer
core/                virtual hardware, firmware build and runtime
app/                 DPF app and panel UI
tools/               champi-headless, panel layout extractor
tests/
docs/                design and planning documents
```

## Building

Not buildable yet. Once the skeleton is in place, the build will be:

```sh
git clone --recurse-submodules git@github.com:pablo-penovi/CHAMPI.git
cd CHAMPI
cmake -B build
cmake --build build
```

## Documentation

- [docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md): the implementation plan, split into chunks, with progress.
- [docs/planning/DETAILED_OVERALL_PLAN.md](docs/planning/DETAILED_OVERALL_PLAN.md): the detailed design.

## Trademarks

CHAMPI is an unofficial project and is not affiliated with CHOMPI Club. The CHOMPI name, logo,
character and related marks are trademarks of CHOMPI Club and are not covered by the MIT licence
of the CHOMPI sources. See CHOMPI's
[TRADEMARKS.md](https://github.com/CHOMPI-Club/CHOMPI/blob/main/TRADEMARKS.md). No CHOMPI art
or brand assets are included in this repo.
