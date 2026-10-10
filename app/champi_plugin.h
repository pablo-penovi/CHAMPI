// The DPF side of CHAMPI: the firmware's audio and MIDI on the host's ports.
//
// The firmware runs once per process (see Runtime), so there is one ChampiPlugin per process too.
// It starts TAPE on daisycola's host audio clock and drives it from run(): host MIDI goes through
// the MIDI controller mapping to the panel or into the virtual TRS port, the firmware's MIDI comes
// back out, and audio goes through HostAudio.
#pragma once

#include "DistrhoPlugin.hpp"
#include "audio_levels.h"
#include "host_audio.h"
#include "midi_map.h"
#include "midi_splitter.h"

START_NAMESPACE_DISTRHO

class ChampiPlugin : public Plugin
{
  public:
    ChampiPlugin();
    ~ChampiPlugin() override;

    /** The audio load figures, for the UI. */
    champi::AudioLoad Load() { return audio_.Load(); }

    /** Sets the gains of the host's inputs and outputs to the volumes. Any thread. */
    void SetLevels(const champi::AudioLevels& levels);

    /** True if the host doesn't run at 48 kHz, so audio is resampled. */
    bool Resampling() const { return audio_.Resampling(); }

    /** The MIDI controller mapping incoming MIDI goes through; the UI sets it and learns with it. */
    champi::MidiMapper& MidiMap() { return midi_map_; }

  protected:
    const char* getLabel() const override { return "CHAMPI"; }
    const char* getDescription() const override
    {
        return "The CHOMPI TAPE firmware on a virtual Daisy Seed";
    }
    const char* getMaker() const override { return ""; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t    getVersion() const override { return d_version(0, 5, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override;
    void initParameter(uint32_t, Parameter&) override {}
    float getParameterValue(uint32_t) const override { return 0.f; }
    void  setParameterValue(uint32_t, float) override {}

    void activate() override;
    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midi,
             uint32_t midi_count) override;

  private:
    champi::HostAudio     audio_;
    champi::MidiSplitter  midi_out_;
    champi::MidiMapper    midi_map_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChampiPlugin)
};

END_NAMESPACE_DISTRHO
