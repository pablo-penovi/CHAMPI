# Changelog

All notable changes to CHAMPI are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/). The version itself lives in `VERSION.md`: bumping it on
`main` releases it from CI.

## [Unreleased]

### Added

- This changelog.

## [1.2.0] - 2026-10-10

### Added

- **MIDI controller mapping**, from a new row in the F8 menu. It lists the panel's 41 controls:
  each knob's rotation and press, the CHAMPI, play and loop buttons, the toggle and Keys 1–25
  ([#20](https://github.com/pablo-penovi/CHAMPI/pull/20)).
  - `Enter` or a click learns a row from the controller. A press is learnt from its first note or
    CC. A rotation takes six CCs and is guessed as absolute or as `relative-64`, `relative-twos`
    or `relative-signed`; `Left`/`Right` overrides the guess.
  - `Delete` unmaps a row, and `Tab` picks the controller when there are several.
  - Each controller's mapping is saved in `~/.config/champi/midi-mappings/` and comes back when
    that port is connected to MIDI in again.
  - Relative rotations turn a detent per step, with the mouse's acceleration. Absolute ones become
    TAPE's own knob CCs (CC20–25) on its MIDI in channel. Unmapped messages reach TAPE as before.

### Changed

- The white knobs have eight identical grip ridges instead of a pointer. The encoders turn
  endlessly, so a pointer suggested a zero position they don't have
  ([#20](https://github.com/pablo-penovi/CHAMPI/pull/20)).
- `champi` sets `PIPEWIRE_LATENCY=128/48000` before JACK opens, unless it's already set, so a key
  press registers in about 20 ms ([#19](https://github.com/pablo-penovi/CHAMPI/pull/19)).

### Fixed

- Quick taps on the computer keyboard animated the key but never reached TAPE. Each button level
  is now held until the firmware has read it in 10 different milliseconds, so every tap plays,
  whatever the buffer size ([#19](https://github.com/pablo-penovi/CHAMPI/pull/19)).

## [1.1.0] - 2026-10-10

### Added

- **Output volumes** for master and phones in the connections menu, saved with the input levels in
  `~/.config/champi/audio_levels.toml`. At 50% an output passes the firmware's sound unchanged;
  100% is +18 dB ([#18](https://github.com/pablo-penovi/CHAMPI/pull/18)).
- **Headphone jack** on the panel (click it, or `F11`): phones play with a plug in, master without,
  as on the device ([#18](https://github.com/pablo-penovi/CHAMPI/pull/18)).
- Demo GIFs of the boot, playing and looping in the README
  ([#17](https://github.com/pablo-penovi/CHAMPI/pull/17)).

### Changed

- `input_levels.toml` is replaced by `audio_levels.toml`. The old file is still read when there's
  no new one yet ([#18](https://github.com/pablo-penovi/CHAMPI/pull/18)).
- The Release workflow uses Node 24 actions and is pinned to `ubuntu-24.04`
  ([#16](https://github.com/pablo-penovi/CHAMPI/pull/16)).
- The README has a full disclosure section ([#15](https://github.com/pablo-penovi/CHAMPI/pull/15)).

### Fixed

- After switching to Cubbi mode, the first press of each cubbi voice was silent. daisycola now
  makes `f_size()` behave as on the STM32; the firmware stays unpatched
  ([#18](https://github.com/pablo-penovi/CHAMPI/pull/18)). Rebuild clean after updating
  (`cmake --build build --clean-first`), or an incremental build keeps the bug.

## [1.0.0] - 2026-10-09

The first release: the real CHOMPI TAPE 2.0 firmware, unpatched, running natively on Linux.

### Added

- **TAPE on a virtual Daisy Seed.** The firmware sources are compiled and linked against
  [daisycola](https://github.com/pablo-penovi/daisycola)
  ([#1](https://github.com/pablo-penovi/CHAMPI/pull/1),
  [#2](https://github.com/pablo-penovi/CHAMPI/pull/2)).
- **Virtual SD card**: a disk image seeded with the factory TAPE card, with import, export and
  reset commands. It can be pulled out while TAPE runs
  ([#3](https://github.com/pablo-penovi/CHAMPI/pull/3),
  [#10](https://github.com/pablo-penovi/CHAMPI/pull/10)).
- **CHOMPI board model**: keys, encoders, LEDs, battery charger, audio and MIDI
  ([#4](https://github.com/pablo-penovi/CHAMPI/pull/4)).
- **`champi-headless`**, which plays the firmware from a script and records its audio, LEDs and
  MIDI out, with golden tests built on it
  ([#6](https://github.com/pablo-penovi/CHAMPI/pull/6),
  [#10](https://github.com/pablo-penovi/CHAMPI/pull/10)).
- **`champi`, a standalone JACK app** built with DPF, which also runs under PipeWire: mic and line
  inputs, master and headphone outputs, MIDI in and out
  ([#7](https://github.com/pablo-penovi/CHAMPI/pull/7)).
- **The panel**, laid out from the CHOMPI board files and drawn as vectors with live LEDs, the
  reference's line art, the USB-C socket and the micro-SD slot. It plays by mouse, and takes
  optional PNG skins ([#8](https://github.com/pablo-penovi/CHAMPI/pull/8),
  [#10](https://github.com/pablo-penovi/CHAMPI/pull/10),
  [#11](https://github.com/pablo-penovi/CHAMPI/pull/11)).
- **Computer keyboard control** with a configurable keymap in `~/.config/champi/keymap.toml`
  ([#9](https://github.com/pablo-penovi/CHAMPI/pull/9)).
- **Connections menu** (`F8`) for audio and MIDI ports, saved to
  `~/.config/champi/connections.toml` and restored when a saved port appears
  ([#12](https://github.com/pablo-penovi/CHAMPI/pull/12)).
- **Input volumes** for mic and line in, in the connections menu
  ([#13](https://github.com/pablo-penovi/CHAMPI/pull/13)).
- `champi --test-mode` for TAPE's factory test
  ([#10](https://github.com/pablo-penovi/CHAMPI/pull/10)).
- **Releases from CI** whenever `VERSION.md` is newer than the latest release, with a Linux x86_64
  tarball ([#14](https://github.com/pablo-penovi/CHAMPI/pull/14)).
- A logo and title in the README ([#5](https://github.com/pablo-penovi/CHAMPI/pull/5)).

### Fixed

- Knobs dropped or reversed turns under JACK, when the firmware read the encoders in bursts
  ([#13](https://github.com/pablo-penovi/CHAMPI/pull/13)).
- The mode switch's lever is drawn up for record mode and down for playback
  ([#13](https://github.com/pablo-penovi/CHAMPI/pull/13)).

[Unreleased]: https://github.com/pablo-penovi/CHAMPI/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/pablo-penovi/CHAMPI/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/pablo-penovi/CHAMPI/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/pablo-penovi/CHAMPI/releases/tag/v1.0.0
