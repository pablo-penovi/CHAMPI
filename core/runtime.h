// The CHOMPI running TAPE on daisycola's virtual MCU: TAPE's firmware library loaded, the board
// model wired up, a card in and the firmware on its own thread.
//
// There is one Runtime per process. Start loads TAPE and starts it; PowerCycle switches it off and
// on with another card, as the CHOMPI must be to read a new card. The panel and the charger are
// host objects: they carry on across a power cycle and can be driven from any host thread.
// Start, PowerCycle and Stop are called from one thread.
//
// The host keeps no pointer into the firmware library (see daisycola's host.h): Booted looks up
// TAPE's boot flags afresh each time, under the lock a power cycle holds.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

#include "card_check.h"
#include "daisycola/host.h"
#include "mp2722.h"
#include "panel_state.h"

namespace champi
{
class Runtime
{
  public:
    /** The process's runtime. */
    static Runtime& Get();

    /** TAPE's firmware library: next to the executable if it's there, as in a release, else the
     *  one in the build tree. */
    static std::filesystem::path FirmwareLibrary();

    /** Loads TAPE's library, wires the panel and the charger, inserts the card and starts TAPE.
     *  With an empty `card_dir`, only loads the library: TAPE starts with the first PowerCycle.
     *  Throws daisycola::FirmwareError if the library can't be loaded, and std::logic_error if
     *  called twice.
     *
     *  With `test_mode`, ENC6 is pushed before the firmware starts, as when it's held at power-on,
     *  so TAPE opens its factory test. The caller lets go of it once the firmware has booted. */
    void Start(const std::filesystem::path& card_dir,
               daisycola::AudioClock         clock     = daisycola::AudioClock::kInternal,
               bool                          test_mode = false);

    /**
     * Switches the CHOMPI off and on with `card_dir` in: halts TAPE, unloads and reloads its
     * library, wires the panel and the charger again, checks the card once more, inserts it and
     * starts TAPE. If the card no longer passes the check, `fallback` (the card that was in) goes
     * in instead, and the problems are returned; with no fallback, TAPE isn't started then.
     *
     * Throws daisycola::FirmwareError if TAPE doesn't halt in time (it keeps running) or the
     * library can't be unloaded or loaded again (no firmware runs then; the process should end).
     */
    std::vector<CardProblem> PowerCycle(const std::filesystem::path& card_dir,
                                        const std::filesystem::path& fallback);

    /** True while TAPE runs: started, and not halted. */
    bool Running() const { return running_.load(); }

    /** True once TAPE has checked the card and started its rainbow: the panel then works. */
    bool Booted() const;

    /** Waits for the firmware to finish any card access in progress and halts it. Returns false if
     *  it didn't stop within the timeout. */
    bool Stop(uint32_t timeout_ms = 3000);

    /** The card in, or empty if there's none. */
    std::filesystem::path Card() const;

    /** TAPE's MIDI in channel, 0-15, read from the card's options.json when it went in. TAPE reads
     *  it at boot too, so it holds until the next power cycle. */
    int MidiInChannel() const { return midi_in_channel_.load(); }

    PanelState& Panel() { return panel_; }
    Mp2722&     Charger() { return charger_; }

  private:
    Runtime() = default;

    // Wires the board, inserts the card and starts TAPE on the loaded library.
    void Boot(const std::filesystem::path& card_dir, bool test_mode);

    PanelState               panel_;
    Mp2722                   charger_;
    daisycola::AudioClock    clock_ = daisycola::AudioClock::kInternal;
    std::filesystem::path    library_;
    std::filesystem::path    card_;
    mutable std::mutex       lock_; // held by PowerCycle and Booted's lookups, and guards card_
    std::atomic<int>         midi_in_channel_{0};
    std::atomic<bool>        loaded_{false};
    bool                     fresh_  = false; // loaded, and not wired since
    bool                     broken_ = false; // a power cycle failed to unload or reload the library
    std::atomic<bool>        running_{false};
    mutable std::atomic<bool> booted_{false}; // seen booted since the last start
};

} // namespace champi
