// champi-headless: runs CHAMPI without a UI. It manages the SD card, and runs the firmware from a
// script (see script.h), writing the master output to a WAV file and the LEDs and script events to
// a log.
//
// The log has one line per event: `<ms> <frame> <event>`, where ms is the time since the firmware
// started and frame is how many audio frames the WAV holds at that point. Events are `start`,
// `booted`, each script command as written, `leds` with the 35 LED colours as rrggbb (KEY1-28,
// ENC1-6, then ENC5's second LED) whenever they change, `midiout` with each message the firmware
// sends on the TRS jack, in hex, and `end`.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "daisycola/host.h"
#include "led_frame.h"
#include "midi_splitter.h"
#include "runtime.h"
#include "script.h"
#include "sd_cli.h"
#include "wav_writer.h"

namespace fs = std::filesystem;

namespace champi
{
namespace
{
struct RunOptions
{
    std::optional<fs::path> script; // "-" is stdin
    std::optional<fs::path> wav;
    std::optional<fs::path> log;
};

void PrintUsage()
{
    std::printf("Usage: champi-headless [options]\n\n"
                "Firmware run:\n"
                "  --script <file>     run the firmware from a script (- for stdin)\n"
                "  --wav <file>        write the master output, as 32-bit float stereo\n"
                "  --log <file>        write the LED and event log\n\n"
                "SD card (runs before the script):\n%s",
                kSdUsage);
}

RunOptions ParseRunOptions(std::vector<std::string>& args)
{
    RunOptions               options;
    std::vector<std::string> rest;
    for(size_t i = 0; i < args.size(); i++)
    {
        std::string                arg = args[i];
        std::optional<std::string> inline_value;
        if(const auto eq = arg.find('='); arg.rfind("--", 0) == 0 && eq != std::string::npos)
        {
            inline_value = arg.substr(eq + 1);
            arg.resize(eq);
        }
        auto value = [&]() -> fs::path {
            if(inline_value)
                return *inline_value;
            if(i + 1 >= args.size())
                throw std::invalid_argument(arg + " needs a value");
            return args[++i];
        };

        if(arg == "--script")
            options.script = value();
        else if(arg == "--wav")
            options.wav = value();
        else if(arg == "--log")
            options.log = value();
        else
            rest.push_back(args[i]);
    }
    args = std::move(rest);
    return options;
}

std::vector<Command> LoadScript(const fs::path& path)
{
    if(path == "-")
        return ParseScript(std::cin);
    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("can't read " + path.string());
    return ParseScript(in);
}

// One firmware run: drives the panel from the script and records what comes out.
class Session
{
  public:
    explicit Session(const RunOptions& options)
    {
        if(options.wav)
            wav_ = std::make_unique<WavWriter>(*options.wav, 2, 48000);
        if(options.log)
        {
            log_ = std::fopen(options.log->c_str(), "w");
            if(!log_)
                throw std::runtime_error("can't create " + options.log->string());
        }
    }

    ~Session()
    {
        if(log_)
            std::fclose(log_);
    }

    void Run(const fs::path& sd_image, const std::vector<Command>& script)
    {
        Runtime& runtime = Runtime::Get();
        start_           = std::chrono::steady_clock::now();
        runtime.Start(sd_image);
        Log("start");

        try
        {
            for(const Command& c : script)
                Do(runtime, c);
        }
        catch(...)
        {
            runtime.Stop(); // don't exit with the firmware thread still running
            throw;
        }

        Pump();
        if(!runtime.Stop())
            throw std::runtime_error("the firmware didn't halt");
        Pump(); // what the firmware produced before it stopped
        Log("end");
        if(wav_)
            wav_->Close();
    }

  private:
    void Do(Runtime& runtime, const Command& c)
    {
        PanelState& panel = runtime.Panel();
        using Type        = Command::Type;
        Pump();
        if(c.type != Type::kBoot && c.type != Type::kWait)
            Log(c.text);

        switch(c.type)
        {
            case Type::kBoot:
            {
                const auto deadline = Now() + c.ms;
                while(!runtime.Booted())
                {
                    if(Now() >= deadline)
                        throw std::runtime_error("line " + std::to_string(c.line) + ": boot timed out");
                    Idle();
                }
                Log("booted");
                break;
            }
            case Type::kWait:
            {
                const auto deadline = Now() + c.ms;
                while(Now() < deadline)
                    Idle();
                break;
            }
            case Type::kKey: panel.SetKey(c.target, c.value); break;
            case Type::kPush: panel.SetEncoderPushed(c.target, c.value); break;
            case Type::kTurn: panel.TurnEncoder(c.target, c.value); break;
            case Type::kToggle: panel.SetToggle(c.value); break;
            case Type::kLineIn: panel.SetLineIn(c.value); break;
            case Type::kMidi:
                if(daisycola::WriteMidiIn(daisycola::MidiPort::kUart, c.bytes.data(), c.bytes.size())
                   != c.bytes.size())
                    throw std::runtime_error("line " + std::to_string(c.line) + ": MIDI input is full");
                break;
            case Type::kUsb: runtime.Charger().SetUsbPower(c.value); break;
            case Type::kBattery: runtime.Charger().SetBatteryMillivolts(uint32_t(c.value)); break;
            case Type::kInput: inputs_[c.target] = {c.hz, c.level, 0}; break;
            case Type::kSd: runtime.Card().SetInserted(c.value); break;
            case Type::kMidiLoop: midi_loop_ = c.value; break;
            case Type::kMark: break;
        }
    }

    // Milliseconds since the firmware started.
    uint64_t Now() const
    {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start_)
                            .count());
    }

    void Idle()
    {
        Pump();
        const daisycola::BoardState state = daisycola::GetBoardState();
        if(!state.running && !state.sleeping)
            throw std::runtime_error("the firmware stopped running");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Collects the audio and MIDI produced so far, keeps the inputs fed, and logs the LEDs if
    // they changed.
    void Pump()
    {
        PumpMidi();
        static float buffers[daisycola::kMaxAudioChannels][1024];
        float*       out[daisycola::kMaxAudioChannels] = {buffers[0], buffers[1], buffers[2], buffers[3]};
        for(size_t n; (n = daisycola::ReadAudio(out, 1024)) > 0;)
        {
            frames_ += n;
            if(wav_)
            {
                const float* master[2] = {buffers[2], buffers[3]}; // TAPE's outputs 3 and 4
                wav_->Write(master, n);
            }
        }
        FeedInputs();

        LedFrame frame;
        ReadLedFrame(frame);
        if(frame.smt_sequence == 0 && frame.pth_sequence == 0)
            return;
        std::string colours;
        auto        add = [&](const daisycola::Rgb& c) {
            char hex[8];
            std::snprintf(hex, sizeof hex, " %02x%02x%02x", c.r, c.g, c.b);
            colours += hex;
        };
        for(const auto& c : frame.key)
            add(c);
        for(const auto& c : frame.encoder)
            add(c);
        add(frame.encoder5_second);
        if(colours != last_leds_)
        {
            last_leds_ = colours;
            Log("leds" + colours);
        }
    }

    // Logs what the firmware sent on the TRS jack, and loops it back in if the cable is in.
    void PumpMidi()
    {
        uint8_t bytes[256];
        for(size_t n; (n = daisycola::ReadMidiOut(daisycola::MidiPort::kUart, bytes, sizeof bytes)) > 0;)
        {
            if(midi_loop_)
                daisycola::WriteMidiIn(daisycola::MidiPort::kUart, bytes, n);
            for(size_t i = 0; i < n; i++)
                if(midi_out_.Feed(bytes[i]))
                {
                    std::string message = "midiout";
                    for(size_t b = 0; b < midi_out_.Size(); b++)
                    {
                        char hex[4];
                        std::snprintf(hex, sizeof hex, " %02x", midi_out_.Data()[b]);
                        message += hex;
                    }
                    Log(message);
                }
        }
    }

    // Keeps the firmware's input queue kInputLead frames ahead of the output read so far, so an
    // input change reaches it within that time. TAPE's inputs: 1 is the mic, 3 and 4 line in.
    void FeedInputs()
    {
        constexpr uint64_t kInputLead = 1024;
        constexpr size_t   kChunk     = 256;
        static float       mic[kChunk], line[kChunk];
        while(fed_ < frames_ + kInputLead)
        {
            const size_t n = size_t(std::min<uint64_t>(kChunk, frames_ + kInputLead - fed_));
            Sine         m = inputs_[int(Input::kMic)], l = inputs_[int(Input::kLine)];
            for(size_t i = 0; i < n; i++)
            {
                mic[i]  = m.Next();
                line[i] = l.Next();
            }
            const float* in[daisycola::kMaxAudioChannels] = {mic, nullptr, line, line};
            const size_t written                          = daisycola::WriteAudio(in, n);
            inputs_[int(Input::kMic)].Advance(written);
            inputs_[int(Input::kLine)].Advance(written);
            fed_ += written;
            if(written < n)
                break;
        }
    }

    void Log(const std::string& event)
    {
        if(log_)
            std::fprintf(log_, "%llu %llu %s\n", (unsigned long long)Now(),
                         (unsigned long long)frames_, event.c_str());
    }

    // A test tone on an input; silent at 0 Hz.
    struct Sine
    {
        double hz = 0, level = 0, phase = 0; // phase in cycles

        float Next()
        {
            if(hz <= 0)
                return 0;
            const float v = float(level * std::sin(2 * M_PI * phase));
            phase         = std::fmod(phase + hz / 48000, 1.0);
            return v;
        }
        void Advance(size_t frames) { phase = std::fmod(phase + hz * double(frames) / 48000, 1.0); }
    };

    std::unique_ptr<WavWriter>            wav_;
    Sine                                  inputs_[2];
    uint64_t                              fed_       = 0; // input frames queued
    bool                                  midi_loop_ = false;
    MidiSplitter                          midi_out_;
    FILE*                                 log_ = nullptr;
    std::chrono::steady_clock::time_point start_;
    uint64_t                              frames_ = 0;
    std::string                           last_leds_;
};

} // namespace
} // namespace champi

int main(int argc, char** argv)
{
    using namespace champi;
    std::vector<std::string> args(argv + 1, argv + argc);
    try
    {
        for(const auto& arg : args)
            if(arg == "-h" || arg == "--help")
            {
                PrintUsage();
                return 0;
            }

        const SdOptions  sd  = ParseSdOptions(args);
        const RunOptions run = ParseRunOptions(args);
        if(!args.empty())
        {
            std::fprintf(stderr, "champi-headless: unknown argument %s\n", args.front().c_str());
            return 2;
        }
        if(!run.script && (run.wav || run.log))
        {
            std::fprintf(stderr, "champi-headless: --wav and --log need --script\n");
            return 2;
        }
        if(!run.script && !sd.HasCommands())
        {
            PrintUsage();
            return 2;
        }

        const std::vector<Command> script = run.script ? LoadScript(*run.script) : std::vector<Command>{};
        RunSdCommands(sd);
        if(run.script)
            Session(run).Run(sd.image, script);
        return 0;
    }
    catch(const std::invalid_argument& e)
    {
        std::fprintf(stderr, "champi-headless: %s\n", e.what());
        return 2;
    }
    catch(const ScriptError& e)
    {
        std::fprintf(stderr, "champi-headless: %s\n", e.what());
        return 2;
    }
    catch(const std::exception& e)
    {
        std::fprintf(stderr, "champi-headless: %s\n", e.what());
        return 1;
    }
}
