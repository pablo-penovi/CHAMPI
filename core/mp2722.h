// The MP2722 battery charger on the CHOMPI's I2C bus, as a register model.
//
// TAPE reads the six status registers from 0x11 and looks at four bits: IINDPM_STAT (0x11 bit 0),
// VIN_GD (0x12 bit 6), LEGACY_CABLE (0x12 bit 4), CHG_STAT (0x13 bits 7:5, 0b101 = charge done)
// and BATT_LOW_STAT (0x16 bit 4). It moves the BATT_LOW threshold in register 0x0C between 3.0 V
// and 3.3 V to tell a medium battery from a high one, and writes 0x08 to enter shipping mode.
// The model computes those status bits from a power state the host sets; other registers just
// store what the firmware writes.
//
// By default the board is on USB power with a full battery, so the firmware never locks itself
// out. Unplugging USB with the battery under 3.0 V makes it blink and then ask for shipping mode,
// as the device does.
#pragma once

#include <atomic>
#include <cstdint>

#include "daisycola/host.h"

namespace champi
{
class Mp2722 : public daisycola::I2CDevice
{
  public:
    static constexpr int     kBus     = 0; // I2C_1
    static constexpr uint8_t kAddress = 0x3f;
    static constexpr int     kNumRegs = 0x20;

    Mp2722();

    /** Puts the charger on the bus and holds its interrupt line (D31, active low) idle. Once per
     *  process, before the firmware starts. */
    void Attach();

    /** Power on the USB input (VIN_GD). */
    void     SetUsbPower(bool on);
    bool     UsbPower() const;

    /** The battery voltage, compared with the BATT_LOW threshold. */
    void     SetBatteryMillivolts(uint32_t mv);
    uint32_t BatteryMillivolts() const;

    /** Charging finished (CHG_STAT = charge done): the firmware shows a full battery. */
    void     SetChargeDone(bool done);
    bool     ChargeDone() const;

    /** A register as the firmware would read it now. */
    uint8_t Register(uint8_t reg) const;

    /** The BATT_LOW threshold the firmware last set, in millivolts. */
    uint32_t BattLowMillivolts() const;

    // I2CDevice, called on the firmware thread.
    bool Write(const uint8_t* data, size_t size) override;
    bool Read(uint8_t* data, size_t size) override;

  private:
    std::atomic<uint8_t>  regs_[kNumRegs];
    std::atomic<bool>     usb_power_{true};
    std::atomic<uint32_t> battery_mv_{4100};
    std::atomic<bool>     charge_done_{true};
    uint8_t               pointer_ = 0; // firmware thread only
};

} // namespace champi
