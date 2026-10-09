#include "sd_card.h"

#include <cstdlib>
#include <stdexcept>

#include "daisycola/host.h"

namespace fs = std::filesystem;

namespace champi
{
namespace
{
// Opens the image for the lifetime of the object, so it's closed again if a copy throws.
class OpenCard
{
  public:
    explicit OpenCard(const fs::path& image) { daisycola::SdOpenImage(image.string()); }
    ~OpenCard() { daisycola::SdCloseImage(); }
    OpenCard(const OpenCard&)            = delete;
    OpenCard& operator=(const OpenCard&) = delete;
};

} // namespace

fs::path DefaultSdImagePath()
{
    fs::path data_home;
    if(const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        data_home = xdg;
    else if(const char* home = std::getenv("HOME"); home && *home)
        data_home = fs::path(home) / ".local/share";
    else
        throw std::runtime_error("SD card: neither XDG_DATA_HOME nor HOME is set");
    return data_home / "champi" / "sdcard.img";
}

fs::path FactoryCardDir()
{
    return CHAMPI_FACTORY_CARD_DIR;
}

void CreateCard(const fs::path& image, const fs::path& card_dir, uint64_t size)
{
    if(!fs::is_directory(card_dir))
        throw std::runtime_error("SD card: card profile " + card_dir.string() + " not found");
    if(image.has_parent_path())
        fs::create_directories(image.parent_path());

    fs::path tmp = image;
    tmp += ".new";
    try
    {
        daisycola::SdCreateImage(tmp.string(), size);
        {
            OpenCard card(tmp);
            daisycola::SdCopyIn(card_dir.string(), "/");
        }
        fs::rename(tmp, image);
    }
    catch(...)
    {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        throw;
    }
}

bool EnsureCard(const fs::path& image, const fs::path& card_dir)
{
    if(fs::exists(image))
        return false;
    CreateCard(image, card_dir);
    return true;
}

void ImportToCard(const fs::path& image, const fs::path& host_path)
{
    if(!fs::exists(host_path))
        throw std::runtime_error("SD card: " + host_path.string() + " not found");
    OpenCard card(image);
    daisycola::SdCopyIn(host_path.string(), "/");
}

void ExportFromCard(const fs::path& image, const fs::path& host_dir)
{
    OpenCard card(image);
    daisycola::SdCopyOut("/", host_dir.string());
}

} // namespace champi
