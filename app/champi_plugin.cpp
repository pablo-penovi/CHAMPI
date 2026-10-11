#include "champi_plugin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "app.h"
#include "daisycola/host.h"
#include "runtime.h"

START_NAMESPACE_DISTRHO

ChampiPlugin::ChampiPlugin() : Plugin(0, 0, 0), audio_(daisycola::ProcessAudio)
{
    SetLevels(champi::Options().audio_levels);
    try
    {
        // In test mode ENC6 starts pushed; the UI lets go of it once the firmware has booted.
        // Without a card TAPE waits for the first one the UI inserts.
        champi::Runtime::Get().Start(champi::Options().card_dir, daisycola::AudioClock::kHost,
                                     champi::Options().test_mode);
        midi_map_.SetFirmwareChannel(champi::Runtime::Get().MidiInChannel());
    }
    catch(const std::exception& e)
    {
        // DPF has no way to fail a standalone plugin's construction.
        std::fprintf(stderr, "champi: %s\n", e.what());
        std::_Exit(1);
    }
}

void ChampiPlugin::SetLevels(const champi::AudioLevels& levels)
{
    // Host inputs are CHAMPI's ports mic to line R, and host outputs master L to phones R.
    static_assert(champi::kMic == 0 && champi::kLineR == champi::kHostInputs - 1
                      && champi::kPhonesR - champi::kMasterL == champi::kHostOutputs - 1,
                  "host channels follow CHAMPI's ports");
    auto gain = [&](int port) { return champi::AudioLevels::Gain(port, levels.percent[port]); };
    for(size_t i = 0; i < champi::kHostInputs; i++)
        audio_.SetInputGain(i, gain(champi::kMic + int(i)));
    for(size_t o = 0; o < champi::kHostOutputs; o++)
        audio_.SetOutputGain(o, gain(champi::kMasterL + int(o)));
}

ChampiPlugin::~ChampiPlugin()
{
    if(!champi::Runtime::Get().Stop())
        std::fprintf(stderr, "champi: the firmware didn't halt\n");

    const champi::AudioLoad load = audio_.Load();
    std::fprintf(stderr, "champi: %llu xruns, %llu late blocks, %llu dropouts\n",
                 (unsigned long long)champi::g_xruns.load(), (unsigned long long)load.late_blocks,
                 (unsigned long long)load.dropouts);
}

void ChampiPlugin::initAudioPort(bool input, uint32_t index, AudioPort& port)
{
    static const char* const kIns[][2]  = {{"mic", "Mic"}, {"line_l", "Line In L"},
                                           {"line_r", "Line In R"}};
    static const char* const kOuts[][2] = {{"master_l", "Master L"}, {"master_r", "Master R"},
                                           {"phones_l", "Phones L"}, {"phones_r", "Phones R"}};
    const auto& names = input ? kIns[index] : kOuts[index];
    port.symbol       = names[0];
    port.name         = names[1];
    port.groupId      = kPortGroupNone;
}

void ChampiPlugin::activate()
{
    // DPF deactivates and reactivates around buffer-size and rate changes, so this covers them.
    audio_.Prepare(getSampleRate(), getBufferSize());
}

void ChampiPlugin::run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midi,
                       uint32_t midi_count)
{
    // Every connected controller arrives merged on one JACK port. What the mapping maps works the
    // panel, or becomes TAPE's own CC for a knob; the rest goes to the TRS jack as it is.
    champi::PanelState& panel = champi::Runtime::Get().Panel();
    for(uint32_t i = 0; i < midi_count; i++)
    {
        const MidiEvent& e    = midi[i];
        const uint8_t*   data = e.size > MidiEvent::kDataSize ? e.dataExt : e.data;
        uint8_t          out[3];
        switch(midi_map_.Process(data, e.size, panel, out))
        {
            case champi::MidiMapper::Result::kPass:
                daisycola::WriteMidiIn(daisycola::MidiPort::kUart, data, e.size);
                break;
            case champi::MidiMapper::Result::kFirmware:
                daisycola::WriteMidiIn(daisycola::MidiPort::kUart, out, sizeof out);
                break;
            case champi::MidiMapper::Result::kConsumed: break;
        }
    }

    audio_.SetPhones(champi::Runtime::Get().Panel().Phones());
    audio_.Run(inputs, outputs, frames);

    uint8_t bytes[256];
    for(size_t n; (n = daisycola::ReadMidiOut(daisycola::MidiPort::kUart, bytes, sizeof bytes)) > 0;)
        for(size_t i = 0; i < n; i++)
            if(midi_out_.Feed(bytes[i]))
            {
                MidiEvent e{};
                e.frame = 0;
                e.size  = uint32_t(midi_out_.Size());
                std::memcpy(e.data, midi_out_.Data(), midi_out_.Size());
                writeMidiEvent(e);
            }
}

Plugin* createPlugin()
{
    return new ChampiPlugin();
}

END_NAMESPACE_DISTRHO
