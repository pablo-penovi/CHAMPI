#include "mp2722.h"

#include "daisy_seed.h"

namespace champi
{
namespace
{
constexpr uint8_t kRegBattLow   = 0x0c;
constexpr uint8_t kRegStatus0   = 0x11; // IINDPM_STAT in bit 0
constexpr uint8_t kRegStatus1   = 0x12; // VIN_GD in bit 6, LEGACY_CABLE in bit 4
constexpr uint8_t kRegStatus2   = 0x13; // CHG_STAT in bits 7:5
constexpr uint8_t kRegStatus5   = 0x16; // BATT_LOW_STAT in bit 4
constexpr uint8_t kChgStatDone  = 0b101;

// TAPE's 3.0 V setting. The chip's power-on value isn't known; TAPE only ever writes this one and
// its 3.3 V setting (0b01011101).
constexpr uint8_t kBattLowReset = 0b01010001;

bool IsStatus(uint8_t reg)
{
    return reg >= kRegStatus0 && reg <= kRegStatus5;
}
} // namespace

Mp2722::Mp2722()
{
    for(auto& r : regs_)
        r.store(0);
    regs_[kRegBattLow].store(kBattLowReset);
}

void Mp2722::Attach()
{
    daisycola::AttachI2CDevice(kBus, kAddress, this);
    daisycola::SetPin(daisy::seed::D31, true);
}

void Mp2722::SetUsbPower(bool on)
{
    usb_power_.store(on);
}

bool Mp2722::UsbPower() const
{
    return usb_power_.load();
}

void Mp2722::SetBatteryMillivolts(uint32_t mv)
{
    battery_mv_.store(mv);
}

uint32_t Mp2722::BatteryMillivolts() const
{
    return battery_mv_.load();
}

void Mp2722::SetChargeDone(bool done)
{
    charge_done_.store(done);
}

bool Mp2722::ChargeDone() const
{
    return charge_done_.load();
}

// Bits 3:2 of 0x0C. TAPE's two values give 0b00 = 3.0 V and 0b11 = 3.3 V; the steps between are
// taken as 100 mV.
uint32_t Mp2722::BattLowMillivolts() const
{
    return 3000 + 100 * (regs_[kRegBattLow].load() >> 2 & 0b11);
}

uint8_t Mp2722::Register(uint8_t reg) const
{
    if(reg >= kNumRegs)
        return 0;
    switch(reg)
    {
        case kRegStatus1: return usb_power_.load() ? 1 << 6 : 0;
        case kRegStatus2: return charge_done_.load() ? kChgStatDone << 5 : 0;
        case kRegStatus5: return battery_mv_.load() < BattLowMillivolts() ? 1 << 4 : 0;
        default: return IsStatus(reg) ? 0 : regs_[reg].load();
    }
}

// The first byte sets the register pointer; any more are written from there on, as on the chip.
// The status registers are read-only.
bool Mp2722::Write(const uint8_t* data, size_t size)
{
    if(size == 0)
        return true;
    pointer_ = data[0];
    for(size_t i = 1; i < size; i++, pointer_++)
        if(pointer_ < kNumRegs && !IsStatus(pointer_))
            regs_[pointer_].store(data[i]);
    return true;
}

bool Mp2722::Read(uint8_t* data, size_t size)
{
    for(size_t i = 0; i < size; i++, pointer_++)
        data[i] = Register(pointer_);
    return true;
}

} // namespace champi
