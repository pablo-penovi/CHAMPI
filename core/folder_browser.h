// The model behind CHAMPI's folder picker (app/folder_picker.h): which folder it shows, its
// subfolders, going up and into them, going to a typed path, and checking a new folder's name.
// Plain std::filesystem, so it's tested without a window.
//
// Only folders are listed, since only folders can be cards. Hidden folders (names starting with
// a dot) are left out. ".." comes first, except at "/".
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace champi
{
/** One line of text being edited, with a cursor. Text is UTF-8, and the cursor moves by whole
 *  characters. */
class TextField
{
  public:
    void Set(std::string text);
    const std::string& Text() const { return text_; }
    /** The cursor, as a byte offset into Text(). */
    size_t Cursor() const { return cursor_; }

    /** Inserts typed or pasted text at the cursor. Line breaks and other control characters are
     *  dropped. */
    void Insert(const std::string& text);
    void Backspace();
    void Delete();
    void Left();
    void Right();
    void Home() { cursor_ = 0; }
    void End() { cursor_ = text_.size(); }

  private:
    std::string text_;
    size_t      cursor_ = 0;
};

class FolderBrowser
{
  public:
    enum class Mode
    {
        kSelect, // choose an existing folder
        kCreate, // name a new folder inside the one shown
    };

    struct Entry
    {
        std::string name; // ".." for the parent
        bool        up = false;
    };

    /** Shows `start`, or the nearest folder above it that exists, in a mode. */
    void Open(Mode mode, const std::filesystem::path& start);

    Mode                         GetMode() const { return mode_; }
    const std::filesystem::path& Path() const { return path_; }
    const std::vector<Entry>&    Entries() const { return entries_; }
    /** Why the folder can't be listed, or why the last move failed; empty if all's well. */
    const std::string& Error() const { return error_; }

    /** The selected entry, or -1 if there are none. */
    int  Selected() const { return selected_; }
    void Select(int index);
    void Move(int delta);

    /** The folder an entry stands for. */
    std::filesystem::path EntryPath(int index) const;

    /** Opens an entry: into a subfolder, or up for "..". */
    void Open(int index);
    /** Goes to the parent folder; nothing at "/". */
    void Up();
    /** Goes to a typed path: absolute, ~ for the home folder, or relative to the folder shown.
     *  A path that doesn't exist or isn't a folder leaves the browser where it was and sets
     *  Error(); a folder that can't be read is shown, empty, with the error. Returns false if it
     *  didn't move. */
    bool GoTo(const std::string& typed);
    /** Lists the folder again. */
    void Refresh();

    /** Why `name` can't be a new folder's name, or empty if it can. */
    static std::string CheckNewName(const std::string& name);

    /** Create mode: where the new folder would go, inside the folder shown. Empty, with `error`
     *  set, if the name isn't allowed. */
    std::filesystem::path NewFolder(const std::string& name, std::string& error) const;

  private:
    void Show(const std::filesystem::path& path);

    Mode                  mode_ = Mode::kSelect;
    std::filesystem::path path_;
    std::vector<Entry>    entries_;
    std::string           error_;
    int                   selected_ = -1;
};

} // namespace champi
