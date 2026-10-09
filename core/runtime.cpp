#include "runtime.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

int chompi_fw_main();

// TAPE's boot flags (chompi_main.cpp): booting clears once the factory-card check is done, and
// rainbow_done is set when the boot rainbow starts. Only read here.
extern bool booting;
extern bool rainbow_done;

#if defined(__SANITIZE_THREAD__)
// The firmware writes these plain bools and Booted polls them. The race is harmless: a stale read
// only makes the host wait a little longer.
extern "C" void
AnnotateBenignRaceSized(const char* file, int line, const volatile void* mem, long size, const char* desc);
#define BENIGN_RACE(var) AnnotateBenignRaceSized(__FILE__, __LINE__, &(var), sizeof(var), #var)
#else
#define BENIGN_RACE(var)
#endif

#if defined(__SANITIZE_ADDRESS__)
// TAPE zeroes the SDRAM daisycola maps at 0xC0000000, which is in ASan's shadow gap. Any program
// that runs the firmware links this file, so it gets the option without setting ASAN_OPTIONS.
extern "C" const char* __asan_default_options()
{
    return "protect_shadow_gap=0";
}
#endif

namespace champi
{
Runtime& Runtime::Get()
{
    static Runtime runtime;
    return runtime;
}

void Runtime::Start(const std::filesystem::path& sd_image, daisycola::AudioClock clock)
{
    if(started_)
        throw std::logic_error("the firmware runs only once per process");
    started_ = true;

    daisycola::SdOpenImage(sd_image.string());
    panel_.Attach();
    charger_.Attach();
    daisycola::SetAudioClock(clock);

    BENIGN_RACE(booting);
    BENIGN_RACE(rainbow_done);
    daisycola::Start(chompi_fw_main);
}

bool Runtime::Booted() const
{
    return started_ && !__atomic_load_n(&booting, __ATOMIC_RELAXED)
           && __atomic_load_n(&rainbow_done, __ATOMIC_RELAXED);
}

bool Runtime::Stop(uint32_t timeout_ms)
{
    if(!started_ || stopped_)
        return true;

    using clock         = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while(daisycola::SdBusy() && clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
    if(!daisycola::Halt(uint32_t(std::max<int64_t>(left.count(), 1))))
        return false;
    stopped_ = true;
    daisycola::SdCloseImage();
    return true;
}

} // namespace champi
