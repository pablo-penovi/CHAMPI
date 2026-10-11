#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

// A fresh directory under the system temp dir, removed when the object goes away.
class TempDir
{
  public:
    TempDir()
    {
        std::string tmpl = (std::filesystem::temp_directory_path() / "champi-XXXXXX").string();
        path_            = mkdtemp(tmpl.data());
    }
    ~TempDir() { std::filesystem::remove_all(path_); }

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

  private:
    std::filesystem::path path_;
};

inline std::string ReadHostFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

inline void WriteHostFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A WAV file's bytes. The defaults make one TAPE plays: 48 kHz, 16-bit, stereo PCM.
struct WavSpec
{
    uint16_t    format   = 1;
    uint16_t    channels = 2;
    uint32_t    rate     = 48000;
    uint16_t    bits     = 16;
    uint32_t    data     = 400;   // bytes in the data chunk
    std::string before_data;      // whole chunks between fmt and data
    int64_t     data_claims = -1; // what the data chunk's header says, if not its size
};

inline std::string Le(uint32_t v, int bytes)
{
    std::string s;
    for(int i = 0; i < bytes; i++)
        s += char(v >> (8 * i) & 0xff);
    return s;
}

inline std::string MakeWav(const WavSpec& w = {})
{
    const uint16_t align = uint16_t(w.channels * w.bits / 8);
    std::string    body  = "WAVE";
    body += "fmt " + Le(16, 4) + Le(w.format, 2) + Le(w.channels, 2) + Le(w.rate, 4) + Le(w.rate * align, 4)
            + Le(align, 2) + Le(w.bits, 2);
    body += w.before_data;
    body += "data" + Le(uint32_t(w.data_claims >= 0 ? w.data_claims : w.data), 4) + std::string(w.data, '\1');
    return "RIFF" + Le(uint32_t(body.size()), 4) + body;
}
