// The CHOMPI running TAPE on daisycola's virtual MCU: the board model wired up, the SD card in and
// the firmware on its own thread.
//
// daisycola runs one firmware per process, so there is one Runtime per process too. Start wires
// the board and starts the firmware; the panel and the charger can then be driven from any host
// thread. Stop halts the firmware so the card image can be read or the process can exit.
#pragma once

#include <cstdint>
#include <filesystem>

#include "card_slot.h"
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

    /** Inserts the card image, wires the panel and the charger, and starts TAPE. Throws if the
     *  firmware was started before in this process, or the image can't be opened.
     *
     *  With `test_mode`, ENC6 is pushed before the firmware starts, as when it's held at power-on,
     *  so TAPE opens its factory test. The caller lets go of it once the firmware has booted. */
    void Start(const std::filesystem::path& sd_image,
               daisycola::AudioClock         clock     = daisycola::AudioClock::kInternal,
               bool                          test_mode = false);

    /** True once TAPE has checked the card and started its rainbow: the panel then works. */
    bool Booted() const;

    /** Waits for the firmware to finish any card access in progress, halts it and closes the
     *  image. Returns false if it didn't stop within the timeout. */
    bool Stop(uint32_t timeout_ms = 3000);

    PanelState& Panel() { return panel_; }
    Mp2722&     Charger() { return charger_; }
    CardSlot&   Card() { return card_; }

  private:
    Runtime() = default;

    PanelState panel_;
    Mp2722     charger_;
    CardSlot   card_;
    bool       started_ = false;
    bool       stopped_ = false;
};

} // namespace champi
