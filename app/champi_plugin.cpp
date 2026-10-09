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
    try
    {
        champi::Runtime::Get().Start(champi::Options().sd_image, daisycola::AudioClock::kHost);
    }
    catch(const std::exception& e)
    {
        // DPF has no way to fail a standalone plugin's construction.
        std::fprintf(stderr, "champi: %s\n", e.what());
        std::_Exit(1);
    }
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
    // Every connected controller arrives merged on one JACK port, and goes to the TRS jack.
    for(uint32_t i = 0; i < midi_count; i++)
    {
        const MidiEvent& e = midi[i];
        daisycola::WriteMidiIn(daisycola::MidiPort::kUart, e.size > MidiEvent::kDataSize ? e.dataExt : e.data,
                               e.size);
    }

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
