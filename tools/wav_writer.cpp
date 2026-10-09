#include "wav_writer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace champi
{
namespace
{
void Put16(uint8_t*& p, uint16_t v)
{
    *p++ = v & 0xff;
    *p++ = v >> 8;
}

void Put32(uint8_t*& p, uint32_t v)
{
    for(int i = 0; i < 4; i++)
        *p++ = (v >> (8 * i)) & 0xff;
}

void PutTag(uint8_t*& p, const char* tag)
{
    std::memcpy(p, tag, 4);
    p += 4;
}
} // namespace

WavWriter::WavWriter(const std::filesystem::path& path, int channels, int sample_rate)
: file_(std::fopen(path.c_str(), "wb")), channels_(channels), sample_rate_(sample_rate)
{
    if(!file_)
        throw std::runtime_error("can't create " + path.string() + ": " + std::strerror(errno));
    WriteHeader();
}

WavWriter::~WavWriter()
{
    if(file_)
        std::fclose(file_);
}

void WavWriter::WriteHeader()
{
    // RIFF header, a WAVE_FORMAT_IEEE_FLOAT fmt chunk and the data chunk header: 44 bytes.
    const uint32_t data_bytes = uint32_t(std::min<uint64_t>(frames_ * channels_ * 4, 0xffffffffu - 36));
    uint8_t        header[44];
    uint8_t*       p = header;
    PutTag(p, "RIFF");
    Put32(p, 36 + data_bytes);
    PutTag(p, "WAVE");
    PutTag(p, "fmt ");
    Put32(p, 16);
    Put16(p, 3); // IEEE float
    Put16(p, uint16_t(channels_));
    Put32(p, uint32_t(sample_rate_));
    Put32(p, uint32_t(sample_rate_ * channels_ * 4));
    Put16(p, uint16_t(channels_ * 4));
    Put16(p, 32);
    PutTag(p, "data");
    Put32(p, data_bytes);
    if(std::fwrite(header, sizeof header, 1, file_) != 1)
        failed_ = true;
}

void WavWriter::Write(const float* const* channels, size_t frames)
{
    std::vector<float> interleaved(frames * channels_);
    for(size_t f = 0; f < frames; f++)
        for(int c = 0; c < channels_; c++)
            interleaved[f * channels_ + c] = channels[c][f];
    if(std::fwrite(interleaved.data(), sizeof(float), interleaved.size(), file_) != interleaved.size())
        failed_ = true;
    frames_ += frames;
}

void WavWriter::Close()
{
    if(!file_)
        return;
    if(std::fseek(file_, 0, SEEK_SET) != 0)
        failed_ = true;
    else
        WriteHeader();
    if(std::fclose(file_) != 0)
        failed_ = true;
    file_ = nullptr;
    if(failed_)
        throw std::runtime_error("writing the WAV file failed");
}

} // namespace champi
