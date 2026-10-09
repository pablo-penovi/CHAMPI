#include "midi_splitter.h"

namespace champi
{
size_t MidiDataBytes(uint8_t status)
{
    switch(status & 0xf0)
    {
        case 0x80:
        case 0x90:
        case 0xa0:
        case 0xb0:
        case 0xe0: return 2;
        case 0xc0:
        case 0xd0: return 1;
    }
    switch(status)
    {
        case 0xf1:
        case 0xf3: return 1;
        case 0xf2: return 2;
    }
    return 0;
}

bool MidiSplitter::Feed(uint8_t byte)
{
    // Real-time bytes never disturb what's in progress.
    if(byte >= 0xf8)
    {
        out_[0]   = byte;
        out_size_ = 1;
        return true;
    }

    if(byte & 0x80)
    {
        sysex_ = byte == 0xf0;
        have_  = 0;
        if(byte == 0xf0 || byte == 0xf7)
        {
            status_ = 0;
            return false;
        }
        // System common messages cancel running status; with no data bytes they're complete now.
        status_ = byte;
        if(MidiDataBytes(byte) == 0)
        {
            status_   = 0;
            out_[0]   = byte;
            out_size_ = 1;
            return true;
        }
        return false;
    }

    // A data byte: dropped inside SysEx or with no status to attach it to.
    if(sysex_ || status_ == 0)
        return false;
    data_[have_++] = byte;
    const size_t need = MidiDataBytes(status_);
    if(have_ < need)
        return false;

    out_[0] = status_;
    for(size_t i = 0; i < need; i++)
        out_[1 + i] = data_[i];
    out_size_ = 1 + need;
    have_     = 0;
    if(status_ >= 0xf0) // only channel messages run on
        status_ = 0;
    return true;
}

} // namespace champi
