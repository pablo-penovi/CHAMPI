// Built into TAPE's firmware library only: reads TAPE's boot flags (chompi_main.cpp) for
// Runtime::Booted, which looks the function up with dlsym and lets go of the library at once.
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

/** True once TAPE has checked the card and started its rainbow. */
extern "C" __attribute__((visibility("default"))) int champi_tape_booted()
{
    BENIGN_RACE(booting);
    BENIGN_RACE(rainbow_done);
    return !__atomic_load_n(&booting, __ATOMIC_RELAXED) && __atomic_load_n(&rainbow_done, __ATOMIC_RELAXED);
}
