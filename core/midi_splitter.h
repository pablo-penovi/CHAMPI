// Splits the raw MIDI bytes the firmware sends into whole messages, as a receiver on the TRS jack
// would: running status is expanded, and real-time bytes (0xf8-0xff) come out on their own, even
// in the middle of another message.
//
// SysEx is read up to its end byte and dropped: TAPE never sends any, and a host event can't
// carry it without a buffer that outlives the audio cycle.
#pragma once

#include <cstddef>
#include <cstdint>

namespace champi
{
class MidiSplitter
{
  public:
    static constexpr size_t kMaxSize = 3;

    /** Takes one byte. Returns true if it completed a message, which Data and Size then hold
     *  until the next call. */
    bool Feed(uint8_t byte);

    const uint8_t* Data() const { return out_; }
    size_t         Size() const { return out_size_; }

  private:
    uint8_t status_   = 0; // running status; 0 = none
    uint8_t data_[2]  = {};
    size_t  have_     = 0; // data bytes collected for status_
    bool    sysex_    = false;
    uint8_t out_[kMaxSize] = {};
    size_t  out_size_ = 0;
};

/** Data bytes that follow a status byte (0x80-0xef or 0xf1-0xf3); 0 for the rest. */
size_t MidiDataBytes(uint8_t status);

} // namespace champi
