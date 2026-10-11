#include "runtime.h"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "sd_card.h"

#if defined(__SANITIZE_ADDRESS__)
// TAPE zeroes the SDRAM daisycola maps at 0xC0000000, which is in ASan's shadow gap. Any program
// that runs the firmware links this file, so it gets the option without setting ASAN_OPTIONS.
extern "C" const char* __asan_default_options()
{
    return "protect_shadow_gap=0";
}
#endif

namespace fs = std::filesystem;

namespace champi
{
namespace
{
// How long TAPE gets to finish a card access and halt before a power cycle.
constexpr uint32_t kHaltTimeoutMs = 3000;
} // namespace

Runtime& Runtime::Get()
{
    static Runtime runtime;
    return runtime;
}

fs::path Runtime::FirmwareLibrary()
{
    std::error_code ec;
    const fs::path  exe = fs::read_symlink("/proc/self/exe", ec);
    if(!ec)
    {
        const fs::path shipped = exe.parent_path() / CHAMPI_FW_TAPE_NAME;
        if(fs::exists(shipped, ec))
            return shipped;
    }
    return CHAMPI_FW_TAPE;
}

void Runtime::Start(const fs::path& card_dir, daisycola::AudioClock clock, bool test_mode)
{
    if(loaded_.load())
        throw std::logic_error("the runtime starts once per process; insert cards with PowerCycle");
    clock_   = clock;
    library_ = FirmwareLibrary();
    daisycola::LoadFirmware(library_.string());
    loaded_.store(true);
    fresh_ = true;
    if(!card_dir.empty())
        Boot(card_dir, test_mode);
}

void Runtime::Boot(const fs::path& card_dir, bool test_mode)
{
    fresh_ = false;
    panel_.Attach();
    charger_.Attach();
    if(test_mode)
        panel_.SetEncoderPushed(6, true);
    daisycola::SdInsert(card_dir.string());
    daisycola::SetAudioClock(clock_);
    midi_in_channel_.store(MidiInChannelOfCard(card_dir));
    {
        std::lock_guard<std::mutex> lock(lock_);
        card_ = card_dir;
    }
    booted_.store(false);
    daisycola::Start();
    running_.store(true);
}

std::vector<CardProblem> Runtime::PowerCycle(const fs::path& card_dir, const fs::path& fallback)
{
    if(broken_)
        throw daisycola::FirmwareError("TAPE couldn't be loaded again after the last power cycle; restart CHAMPI");
    if(!loaded_.load())
        throw std::logic_error("PowerCycle before Start");

    // A library that hasn't been wired since it was loaded is as fresh as after a cycle.
    if(!fresh_)
    {
        // Halt first, with everything still in place: if TAPE doesn't stop, nothing has changed.
        if(!Stop(kHaltTimeoutMs))
            throw daisycola::FirmwareError("TAPE didn't halt within " + std::to_string(kHaltTimeoutMs)
                                           + " ms; it is still running");
        std::lock_guard<std::mutex> lock(lock_);
        panel_.Detach();
        card_.clear();
        try
        {
            daisycola::PowerCycle(kHaltTimeoutMs);
        }
        catch(...)
        {
            loaded_.store(false); // daisycola refuses to load it again: never run stale state
            broken_ = true;
            throw;
        }
        fresh_ = true;
    }

    std::vector<CardProblem> problems = CheckCard(card_dir);
    if(problems.empty())
        Boot(card_dir, false);
    else if(!fallback.empty())
        Boot(fallback, false);
    return problems;
}

bool Runtime::Booted() const
{
    if(booted_.load())
        return true;
    std::lock_guard<std::mutex> lock(lock_);
    if(!running_.load())
        return false;
    // Look the probe up and let go of the library at once: a reference held across a power cycle
    // would keep it loaded.
    void* library = dlopen(library_.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if(!library)
        return false;
    const auto probe  = reinterpret_cast<int (*)()>(dlsym(library, "champi_tape_booted"));
    const bool booted = probe && probe();
    dlclose(library);
    if(booted)
        booted_.store(true);
    return booted;
}

bool Runtime::Stop(uint32_t timeout_ms)
{
    if(!running_.load())
        return true;

    using clock         = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while(daisycola::SdBusy() && clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
    if(!daisycola::Halt(uint32_t(std::max<int64_t>(left.count(), 1))))
        return false;
    running_.store(false);
    return true;
}

fs::path Runtime::Card() const
{
    std::lock_guard<std::mutex> lock(lock_);
    return card_;
}

} // namespace champi
