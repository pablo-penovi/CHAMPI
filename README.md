# CHAMPI

CHAMPI is a native Linux virtual instrument that runs the real
[CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) TAPE firmware on x86.

The CHOMPI is a discontinued, MIT-licensed sampler built on a Daisy Seed (STM32H750,
libDaisy/DaisySP). CHAMPI doesn't rewrite the firmware. It compiles the original firmware sources,
unchanged, against a host version of libDaisy's hardware layer. The firmware then runs on a
"virtual chip" that gives it emulated keys, encoders, LEDs, an SD card, audio and MIDI.

## Status

Early work: the TAPE firmware compiles and links against daisycola, the virtual SD card works, and
the CHOMPI board model (panel wiring, battery charger, LED layout) is in place, but the firmware
doesn't run yet. The work
is split into chunks, each ending in something that builds and has tests. Progress is tracked in
[docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md).

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
third_party/CHOMPI/    submodule: CHOMPI firmware, card profiles and board files (never patched)
third_party/daisycola/ submodule: libDaisy replacement and virtual MCU
third_party/DPF/       submodule: DISTRHO Plugin Framework
core/                  firmware build, CHOMPI board model, runtime glue
app/                   DPF app and panel UI
tools/                 champi-headless, panel layout extractor
tests/
docs/                  design and planning documents
```

## Building

You need gcc, CMake 3.20 or newer, and access to the private daisycola repo. For now the build
compiles the TAPE firmware into `libchampi_fw_tape.a` and builds `champi-headless`, which can only
manage the SD card so far.

```sh
git clone --recurse-submodules git@github.com:pablo-penovi/CHAMPI.git
cd CHAMPI
cmake -B build
cmake --build build
```

In an existing clone, run `git submodule update --init --recursive` first. Run the tests with
`ctest --test-dir build`.

## SD card

The virtual SD card is a 4 GB sparse disk image at `~/.local/share/champi/sdcard.img` (or under
`$XDG_DATA_HOME`). It's created on first use and seeded with the factory TAPE card from
`third_party/CHOMPI/firmware/card-profiles/tape-2.0`.

```sh
build/tools/champi-headless --sd-import ~/samples   # copy a directory's contents to the card root
build/tools/champi-headless --sd-export ~/card      # copy the whole card out
build/tools/champi-headless --sd-reset              # start again from the factory card
```

`--sd-image <file>` uses another image. The image is an MBR disk with one FAT32 partition, so it
can also be loop-mounted or used with mtools.

## Documentation

- [docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md): the implementation plan, split into chunks, with progress.
- [docs/planning/DETAILED_OVERALL_PLAN.md](docs/planning/DETAILED_OVERALL_PLAN.md): the detailed design.

## Licence

MIT, see [LICENSE](LICENSE). The CHOMPI firmware in `third_party/CHOMPI` is MIT-licensed by CHOMPI
Club.

## Trademarks

CHAMPI is an unofficial project and is not affiliated with CHOMPI Club. The CHOMPI name, logo,
character and related marks are trademarks of CHOMPI Club and are not covered by the MIT licence
of the CHOMPI sources. See CHOMPI's
[TRADEMARKS.md](https://github.com/CHOMPI-Club/CHOMPI/blob/main/TRADEMARKS.md). No CHOMPI art
or brand assets are included in this repo.
