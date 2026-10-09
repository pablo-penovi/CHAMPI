#pragma once

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
