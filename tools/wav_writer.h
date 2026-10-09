// Writes 32-bit float WAV files as audio arrives. The sizes in the header are filled in by Close.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace champi
{
class WavWriter
{
  public:
    /** Creates the file. Throws std::runtime_error if it can't. */
    WavWriter(const std::filesystem::path& path, int channels, int sample_rate);
    ~WavWriter();
    WavWriter(const WavWriter&)            = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    /** Appends `frames` frames, one pointer per channel. */
    void Write(const float* const* channels, size_t frames);

    /** Fixes up the header and closes the file. Throws if writing failed. */
    void Close();

    uint64_t Frames() const { return frames_; }

  private:
    void WriteHeader();

    FILE*    file_;
    int      channels_;
    int      sample_rate_;
    uint64_t frames_ = 0;
    bool     failed_ = false;
};

} // namespace champi
