// The CHOMPI's micro-SD slot: whether the card is in. Pulling it out makes every card access fail,
// as on the device; TAPE then blinks its LEDs red and carries on with its built-in sample only.
// Putting it back changes nothing until the firmware restarts, which here means a new process.
//
// The image file stays open throughout (see Runtime), so the card is only "out" to the firmware.
// Any host thread may call these.
#pragma once

#include <atomic>

namespace champi
{
class CardSlot
{
  public:
    /** Puts the card in (true) or pulls it out (false). */
    void SetInserted(bool inserted);
    bool Inserted() const { return inserted_.load(); }

  private:
    std::atomic<bool> inserted_{true};
};

} // namespace champi
