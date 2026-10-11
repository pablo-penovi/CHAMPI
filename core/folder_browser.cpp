#include "folder_browser.h"

#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace champi
{
namespace
{
bool Continuation(char c)
{
    return ((unsigned char)c & 0xc0) == 0x80;
}

// The path as the browser shows it: absolute, without . or .. or a trailing slash.
fs::path Clean(const fs::path& path)
{
    fs::path clean = path.lexically_normal();
    if(clean.has_relative_path() && clean.filename().empty())
        clean = clean.parent_path();
    return clean;
}

} // namespace

// ---- TextField ----------------------------------------------------------------------------------

void TextField::Set(std::string text)
{
    text_   = std::move(text);
    cursor_ = text_.size();
}

void TextField::Insert(const std::string& text)
{
    std::string clean;
    for(char c : text)
        if((unsigned char)c >= 0x20 && c != 0x7f)
            clean += c;
    text_.insert(cursor_, clean);
    cursor_ += clean.size();
}

void TextField::Backspace()
{
    if(cursor_ == 0)
        return;
    const size_t end = cursor_;
    Left();
    text_.erase(cursor_, end - cursor_);
}

void TextField::Delete()
{
    if(cursor_ == text_.size())
        return;
    const size_t start = cursor_;
    Right();
    text_.erase(start, cursor_ - start);
    cursor_ = start;
}

void TextField::Left()
{
    while(cursor_ > 0 && Continuation(text_[--cursor_]))
        ;
}

void TextField::Right()
{
    if(cursor_ < text_.size())
        cursor_++;
    while(cursor_ < text_.size() && Continuation(text_[cursor_]))
        cursor_++;
}

// ---- FolderBrowser ------------------------------------------------------------------------------

void FolderBrowser::Open(Mode mode, const fs::path& start)
{
    mode_ = mode;
    fs::path        path = Clean(fs::absolute(start));
    std::error_code ec;
    while(!fs::is_directory(path, ec) && path.has_relative_path())
        path = path.parent_path();
    Show(path);
}

void FolderBrowser::Show(const fs::path& path)
{
    path_ = Clean(path);
    entries_.clear();
    error_.clear();
    if(path_.has_relative_path())
        entries_.push_back({"..", true});

    std::vector<std::string> names;
    std::error_code          ec;
    for(fs::directory_iterator it(path_, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        std::error_code   dir_ec;
        if(name.empty() || name[0] == '.' || !it->is_directory(dir_ec))
            continue;
        names.push_back(name);
    }
    if(ec)
        error_ = "Can't read this folder: " + ec.message() + ".";
    std::sort(names.begin(), names.end());
    for(std::string& n : names)
        entries_.push_back({std::move(n), false});
    selected_ = entries_.empty() ? -1 : 0;
    // Coming into a folder, the first subfolder is more use than "..".
    if(entries_.size() > 1 && entries_[0].up)
        selected_ = 1;
}

void FolderBrowser::Select(int index)
{
    if(index >= 0 && index < int(entries_.size()))
        selected_ = index;
}

void FolderBrowser::Move(int delta)
{
    if(entries_.empty())
        return;
    selected_ = std::clamp(selected_ + delta, 0, int(entries_.size()) - 1);
}

fs::path FolderBrowser::EntryPath(int index) const
{
    if(index < 0 || index >= int(entries_.size()))
        return path_;
    return entries_[index].up ? path_.parent_path() : path_ / entries_[index].name;
}

void FolderBrowser::Open(int index)
{
    if(index < 0 || index >= int(entries_.size()))
        return;
    if(entries_[index].up)
        Up();
    else
        Show(path_ / entries_[index].name);
}

void FolderBrowser::Up()
{
    if(!path_.has_relative_path())
        return;
    const std::string left = path_.filename().string();
    Show(path_.parent_path());
    // Select the folder just left, so going back in is one key.
    for(size_t i = 0; i < entries_.size(); i++)
        if(!entries_[i].up && entries_[i].name == left)
            selected_ = int(i);
}

bool FolderBrowser::GoTo(const std::string& typed)
{
    fs::path path = typed;
    if(typed == "~" || typed.rfind("~/", 0) == 0)
        if(const char* home = std::getenv("HOME"); home && *home)
            path = fs::path(home) / typed.substr(std::min<size_t>(2, typed.size()));
    if(path.is_relative())
        path = path_ / path;
    path = Clean(path);

    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if(!fs::exists(status))
    {
        error_ = path.string() + " doesn't exist.";
        return false;
    }
    if(!fs::is_directory(status))
    {
        error_ = path.string() + " is a file, not a folder.";
        return false;
    }
    Show(path);
    return true;
}

void FolderBrowser::Refresh()
{
    const int selected = selected_;
    Show(path_);
    Select(selected);
}

std::string FolderBrowser::CheckNewName(const std::string& name)
{
    if(name.empty())
        return "Type a name for the new folder.";
    if(name == "." || name == "..")
        return "\"" + name + "\" can't be a folder's name.";
    if(name.find('/') != std::string::npos)
        return "A folder's name can't hold /.";
    if(name.find('\0') != std::string::npos)
        return "A folder's name can't hold a NUL character.";
    return "";
}

fs::path FolderBrowser::NewFolder(const std::string& name, std::string& error) const
{
    error = CheckNewName(name);
    return error.empty() ? path_ / name : fs::path();
}

} // namespace champi
