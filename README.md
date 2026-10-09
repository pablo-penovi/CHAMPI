<p align="center">
  <img src="champi.png" alt="Champi, a happy mushroom" height="160">
  <img src="docs/assets/champi-title.svg" alt="CHAMPI" height="160">
</p>

CHAMPI is a native Linux virtual instrument that runs the real
[CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) TAPE firmware on x86.

The CHOMPI is a discontinued, MIT-licensed sampler built on a Daisy Seed (STM32H750,
libDaisy/DaisySP). CHAMPI doesn't rewrite the firmware. It compiles the original firmware sources,
unchanged, against a host version of libDaisy's hardware layer. The firmware then runs on a
"virtual chip" that gives it emulated keys, encoders, LEDs, an SD card, a battery charger, audio
and MIDI. Everything TAPE does, it does here because it's the same code: the sampler, the looper,
recording, the shift menus, presets, options and the factory test.

## Features

- **The real TAPE 2.0 firmware**, built from the CHOMPI sources without a single patch, running on
  [daisycola](https://github.com/pablo-penovi/daisycola)'s virtual Daisy Seed with its interrupts,
  timers and DMA behaving as on the chip.
- **An on-screen panel** laid out from the CHOMPI board files, drawn as vectors, with live LEDs.
- **Play it** with the mouse, the computer keyboard (with a configurable keymap) or any number of
  MIDI controllers.
- **A standalone JACK app**, which also runs under PipeWire, built with
  [DPF](https://github.com/DISTRHO/DPF). It has mic and line inputs, master and headphone outputs,
  and MIDI in and out.
- **A virtual SD card**: a disk-image file seeded with the factory TAPE card, with import, export
  and reset commands. You can pull it out while TAPE runs.
- **A headless runner** that plays the firmware from a script and records its audio, LEDs and MIDI
  out. The test suite uses it.

TEMPO and WAVE, CHOMPI's other two firmwares, may follow. Progress is tracked in
[docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md).

## Building

You need gcc, CMake 3.20 or newer, access to the private daisycola repo, and the development
packages for JACK, libsamplerate, OpenGL and X11. The build compiles the TAPE firmware and builds
`champi` and `champi-headless`.

```sh
git clone --recurse-submodules git@github.com:pablo-penovi/CHAMPI.git
cd CHAMPI
cmake -B build
cmake --build build
```

In an existing clone, run `git submodule update --init --recursive` first. Run the tests with
`ctest --test-dir build`; the headless tests run the firmware in real time and take about half a
minute in parallel. For a ThreadSanitizer or AddressSanitizer build, configure a separate build
directory with `-DCHAMPI_SANITIZER=thread` or `-DCHAMPI_SANITIZER=address`.

## Running the app

```sh
build/bin/champi
```

`champi` is a standalone JACK client named `CHAMPI`. Under PipeWire, `pipewire-jack` provides the
JACK library, so no JACK server is needed. It has three inputs (`mic`, `line_l`, `line_r`), four
outputs (`master_l`, `master_r`, `phones_l`, `phones_r`), a MIDI input (`events-in`) and a MIDI
output (`midi-out`). At start it connects master out to the first two playback ports and every
hardware MIDI source to its MIDI input; `--no-connect` turns that off. Other connections can be
made in qpwgraph or with `pw-link`.

The firmware runs at 48 kHz. At other rates the audio is resampled with libsamplerate, which adds a
little latency, so it's better to run the graph at 48 kHz. For a 64-frame buffer:

```sh
PIPEWIRE_QUANTUM=64/48000 build/bin/champi
```

TAPE boots in about a second and plays its rainbow; the panel ignores input until the rainbow has
finished, about five seconds later, as on the device.

| Option | |
|---|---|
| `--no-connect` | don't connect master out and MIDI controllers |
| `--skin <dir>` | panel art (see [Skin](#skin)) |
| `--keymap <file>` | computer keys for the panel (see [Keyboard](#keyboard)) |
| `--print-keymap` | print the keymap in use as `keymap.toml`, and exit |
| `--test-mode` | start in TAPE's factory test (see [Test mode](#test-mode)) |
| `--sd-image <file>` | use another SD-card image |
| `--sd-reset`, `--sd-import <dir>`, `--sd-export <dir>` | manage the card, then exit (see [SD card](#sd-card)) |

The line under the panel shows whether TAPE is running, the rate and buffer size, the share of
each cycle the firmware takes (load), JACK xruns, late blocks (cycles where the firmware didn't
finish in time) and resampler dropouts. The same counts are printed on exit.

### The panel

The panel is laid out from the CHOMPI's own board files. Everything on it works with the mouse:

- **Keys**: click and hold to play. KEY1–15 are the white row, KEY16–25 the black row; the pink key
  is CHOMPI, the cyan one play and the yellow one loop.
- **Encoders**: drag up or down to turn (up is clockwise), or scroll over one. Turned slowly, a
  2 mm drag or one wheel step is one detent; turned fast, each counts for up to four. Click to push
  an encoder; hold the button still for a moment to keep it pushed, then drag to turn it while
  pushed.
- **The toggle switch**: click to flip it (lever up is on). Off is TAPE's record mode.
- **Line in** (the jack at the right edge): click to plug or unplug a cable. Without a plug, TAPE
  records from the mic.
- **USB** (the socket at the front, bottom left): click to plug or unplug USB power. Scroll over it
  to set the battery voltage, between 2.8 V and 4.2 V (full). Its label shows the voltage.
- **SD card** (the slot at the front, bottom right): click to pull the card out or put it back.

The window keeps the panel's proportions and can shrink to half size.

### Keyboard

The window must have focus. Keys are physical, named as on a US keyboard: on other layouts they're
the keys in the same places. These are the defaults:

| Key | Panel |
|---|---|
| `Z` `X` `C` `V` `B` `N` `M` | white keys 1–7 |
| `Q` `W` `E` `R` `T` `Y` `U` `I` | white keys 8–15 |
| `S` `D` | black keys 16–17 (between white keys 1–2 and 2–3) |
| `G` `H` `J` | black keys 18–20 (between white keys 4–5, 5–6 and 6–7) |
| `2` `3` | black keys 21–22 (between white keys 8–9 and 9–10) |
| `5` `6` `7` | black keys 23–25 (between white keys 11–12, 12–13 and 13–14) |
| `Tab` | CHOMPI key |
| `Space` | play |
| `Enter` | loop |
| `F1` | select ENC4, the speed knob (leftmost) |
| `F2` `F3` `F4` | select ENC1, ENC2, ENC3 |
| `F5` | select ENC5, the scrub wheel |
| `F6` | select ENC6, the volume knob (rightmost) |
| `←` or `[` | turn the selected encoder anticlockwise; hold to keep turning |
| `→` or `]` | turn the selected encoder clockwise; hold to keep turning |
| `\` | push the selected encoder while held (turn it meanwhile for push-and-turn) |
| `` ` `` | flip the toggle switch |
| `F9` | pull the SD card out, or put it back |
| `F10` | plug or unplug USB power |
| `F12` | plug or unplug line in |

Panel keys play while held, and chords work. A held turn key turns 20 detents a second, then 40
after a second, then 80. ENC4 is selected at start; once the keyboard has been used, a ring marks
the selected encoder. If the window loses focus, every held key is let go.

To change the keys, start from the defaults:

```sh
build/bin/champi --print-keymap > ~/.config/champi/keymap.toml
```

and edit it. Each line sets an action to a key, a list of keys or `[]`, as in
`turn_left = ["Left", "Minus"]`. Lines can be left out: whatever the file doesn't name keeps its
default. A key with no name can be given by its Linux scancode. `--keymap <file>` reads another
file. A mistake in the file stops `champi` with the line number.

The actions are `key_1` to `key_25`, `chompi`, `play`, `loop`, `toggle`, `line_in`, `usb`,
`sd_card`, `encoder_1` to `encoder_6`, `turn_left`, `turn_right` and `push`.

### MIDI

Every controller connected to `events-in` reaches TAPE as its TRS MIDI input, on the channel set in
`options.json` (channel 1 on the factory card). TAPE handles MIDI itself:

- Notes play the keys, from C1 up four octaves.
- CC20–25 set the six encoders. CC24 (the scrub wheel) only acts while the looper plays.
- CC26 and CC27 work play and loop (above 84 is pressed, below 42 released).

TAPE sends what you play on `midi-out`: notes for the keys, CCs for the encoders and the CHOMPI,
play and loop keys, on the MIDI out channel from `options.json`.

### Skin

CHAMPI ships none of CHOMPI's art. A skin folder can replace the logo and the glyphs on the
CHOMPI, play and loop keys with your own: `logo.png`, `chompi.png`, `play.png` and `loop.png` in
`~/.config/champi/skin` (or under `$XDG_CONFIG_HOME`, or `--skin <dir>`), each optional.

## Playing TAPE

This is a quick tour of what the firmware does. CHAMPI adds nothing: it's all TAPE.

**Samples.** After boot TAPE is in JAMMI mode on slot 15, its built-in sample (a sine). The keys
play the selected sample chromatically: KEY8 at its own pitch, KEY1 an octave down. ENC4, the
leftmost knob, is the speed; ENC1–3 and ENC6 set the rest, and pushing one switches it to its
second page. The LEDs above the knobs show their values.

**Recording a sample.** Flip the toggle off (record mode), then hold the CHOMPI key: TAPE records
from line in if a cable is plugged in, or from the mic. Its LED is red while it records. Let go,
flip the toggle back on, and the keys play the recording. With "Record Latch" on in `options.json`,
one tap starts recording and the next stops it.

**The looper.** Press loop to start recording a loop and play over it. Press loop again to close
the loop; it plays back and overdubs what you play. Press loop once more to stop overdubbing. Play
pauses and resumes; with the factory "Tape Slew" option, a pause slows the loop to a stop like a
tape machine. Hold play and loop together for two seconds to clear it.

**The shift menu.** With the toggle on, hold the CHOMPI key. While it's held:

- A white key picks that slot's sample (JAMMI mode).
- KEY16 picks JAMMI mode, or the next bank (A to E; the factory card fills A to C) if already in
  it; KEY17 does the same for CUBBI mode, where each key plays a different slot.
- KEY18, KEY19 and KEY20 set the record source: mic, line in or resample.
- KEY21 and KEY22 put the effects before or after the looper.
- KEY23 erases, KEY24 copies and KEY25 saves. Press one, pick the slot (for copy, the source and
  then the destination), let go of CHOMPI, and press CHOMPI again to confirm. Save stores the slot
  15 sample and the knobs in the slot you pick.
- Pushing an encoder resets it or toggles its option.

**Presets and options.** TAPE keeps the knob settings of every slot in `presets.json` on the card
and writes it a few seconds after a change. `options.json` holds the global settings: record
latch, the MIDI in and out channels, tape slew, the monitor position, which menu quantizes pitch,
and split delay. TAPE reads it at boot and writes it back. To change one, export the card, edit
the file and import it again:

```sh
build/tools/champi-headless --sd-export /tmp/card
$EDITOR /tmp/card/options.json
mkdir /tmp/opts && cp /tmp/card/options.json /tmp/opts/
build/tools/champi-headless --sd-import /tmp/opts
```

**The battery.** Hold ENC6 for two seconds to see the battery level on its LED. TAPE shows "full"
while the charger reports charging done (4.2 V on the USB socket's wheel), and keeps showing it
for 20 minutes after that ends, as on the device. Unplugging USB with the battery under 3.0 V
makes it lock itself out.

### Test mode

Holding ENC6 while the CHOMPI powers on starts TAPE's factory test. `champi --test-mode` does that:
ENC6 is held until the firmware has booted. With the toggle on, every output plays a 100 Hz test
tone. The test passes when every key and encoder push has been pressed, every encoder turned both
ways, the toggle flipped both ways, a line-in cable plugged in, USB unplugged and plugged in again,
and 20 notes have come back in a row from MIDI out to MIDI in. For that last one, connect
`CHAMPI:midi-out` to `CHAMPI:events-in`. Then press CHOMPI to leave the test.

### Pulling the SD card

Click the SD slot, or press `F9`. TAPE blinks every LED red for three seconds, then carries on
with its built-in sample only: the shift menu won't change slots, and nothing is saved. Putting
the card back changes nothing until TAPE restarts, as on the device: quit and start `champi` again.

## SD card

The virtual SD card is a 4 GB sparse disk image at `~/.local/share/champi/sdcard.img` (or under
`$XDG_DATA_HOME`). It's created on first use and seeded with the factory TAPE card from
`third_party/CHOMPI/firmware/card-profiles/tape-2.0`.

```sh
build/tools/champi-headless --sd-import ~/samples   # copy a directory's contents to the card root
build/tools/champi-headless --sd-export ~/card      # copy the whole card out
build/tools/champi-headless --sd-reset              # start again from the factory card
```

`champi` takes the same options. Given `--sd-reset`, `--sd-import` or `--sd-export`, it runs them
and exits instead of starting. Samples are named `<jammi|cubbi>_<bank><slot>.wav`, as in
`jammi_a1.wav`, in 48 kHz 16-bit stereo. TAPE makes the `_double` versions itself at boot.

`--sd-image <file>` uses another image. The image is an MBR disk with one FAT32 partition, so it
can also be loop-mounted or used with mtools. Don't change it while CHAMPI runs.

## Headless runs

`champi-headless --script <file>` boots the firmware from the SD card and drives it from a
script. It writes the master output to a WAV file and the LEDs, MIDI out and script events to a
log. Everything happens in real time.

```sh
cat > play.txt <<'END'
boot          # wait for TAPE to boot
wait 6s       # the boot rainbow ignores the panel until it's over
key 8 down
wait 1s
key 8 up
wait 500ms
END
build/tools/champi-headless --script play.txt --wav play.wav --log play.log
```

| Command | |
|---|---|
| `boot [<timeout>]` | wait until TAPE has booted (default 60s) |
| `wait <duration>` | let it run, e.g. `250ms`, `2s`, `1.5s` |
| `key <1-28> down\|up` | press or release a key |
| `push <1-6> down\|up` | push or release an encoder |
| `turn <1-6> <detents>` | turn an encoder, e.g. `+10` or `-3` |
| `toggle on\|off` | the toggle switch |
| `linein on\|off` | plug a cable into line in, or pull it out |
| `input mic\|line sine <hz> [<level>]` | play a sine into the mic or line in (level 0–1, default 0.5) |
| `input mic\|line off` | silence it |
| `midi <hex bytes>` | send MIDI to the TRS input, e.g. `midi 90 3c 7f` |
| `midiloop on\|off` | a cable from MIDI out back to MIDI in |
| `usb on\|off` | USB power |
| `battery <millivolts>` | the battery voltage |
| `sd out\|in` | pull the SD card out, or put it back |
| `mark <text>` | a marker in the log |

Commands run one after another, so time passes only in `boot` and `wait`. To start in test mode,
begin with `push 6 down`, `boot` and `push 6 up`.

Each log line is `<ms> <frame> <event>`: the time since start, the WAV frame at that moment, then
the event. Events are the script commands, `booted`, `leds` followed by the 35 LED colours
(KEY1–28, ENC1–6, ENC5's second LED) whenever they change, and `midiout` with each message TAPE
sends.

## Troubleshooting

- **No sound.** Check master out is connected to your playback ports (`--no-connect` turns that
  off), and that the volume knob (ENC6) isn't down. TAPE ignores the panel for the first five
  seconds after boot.
- **Xruns or late blocks.** Run the graph at 48 kHz and a buffer of 64 frames or more. The firmware
  thread runs at normal priority, so a fully loaded machine can delay it.
- **A MIDI controller does nothing.** Check it's connected to `CHAMPI:events-in` and sends on the
  MIDI in channel from `options.json`. Under a JACK2 server without a2jmidid, ALSA-only
  controllers need connecting by hand.
- **The panel stopped changing samples.** The SD card may be out: the status line says so. Put it
  back and restart `champi`.
- **A broken card.** `--sd-reset` starts again from the factory card. Export anything you want to
  keep first.

## Repository layout

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

## Documentation

- [docs/planning/OVERALL_PLAN.md](docs/planning/OVERALL_PLAN.md): the implementation plan, split
  into chunks, with progress and what was built.
- [docs/planning/DETAILED_OVERALL_PLAN.md](docs/planning/DETAILED_OVERALL_PLAN.md): the detailed
  design.

## Licence

MIT, see [LICENSE](LICENSE). The CHOMPI firmware in `third_party/CHOMPI` is MIT-licensed by CHOMPI
Club.

## Trademarks

CHAMPI is an unofficial project and is not affiliated with CHOMPI Club. The CHOMPI name, logo,
character and related marks are trademarks of CHOMPI Club and are not covered by the MIT licence
of the CHOMPI sources. See CHOMPI's
[TRADEMARKS.md](https://github.com/CHOMPI-Club/CHOMPI/blob/main/TRADEMARKS.md). No CHOMPI art
or brand assets are included in this repo.
