// CHAMPI's folder picker: where things sit and what the keys and the mouse do. The window draws it
// over the panel (champi_ui.cpp), in the style of the connections menu; the folder itself is
// FolderBrowser's. None of it needs DPF, so it works the same in every host, with no desktop
// portal or toolkit.
//
//   - The folder's path sits at the top, in a text field: type or paste a path and press Enter to
//     go there.
//   - Below it, the folder's subfolders, ".." first. Up and Down move, Enter or a click opens one,
//     Backspace goes up.
//   - Select mode: "Use this folder" chooses the folder shown, and a double-click chooses an
//     entry.
//   - Create mode: a name field and "Create", which names a new folder inside the one shown.
//   - Tab moves between the list, the fields and the button; Ctrl+Enter is the button anywhere.
//     Escape cancels.
//
// Everything is in panel millimetres.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include "folder_browser.h"
#include "insert_card.h"
#include "panel_layout.h"

namespace champi
{
using Scancode = int;

class FolderPicker : public InsertCard::Picker
{
  public:
    using Clock = std::chrono::steady_clock;

    enum class Focus
    {
        kList,
        kPath,
        kName, // create mode only
        kButton,
    };

    /** What an input did, for the window to pass on to InsertCard. */
    struct Result
    {
        enum class Kind
        {
            kNone,
            kChosen,    // `path` is the folder chosen, or the new folder to create
            kCancelled,
        };
        Kind                  kind = Kind::kNone;
        std::filesystem::path path;
    };

    // Where things are.
    static constexpr layout::Rect kBox{24, 5, 278, 100};
    static constexpr float        kPad        = 4;
    static constexpr float        kTitleY     = kBox.y + 7;
    static constexpr float        kLineHeight = 6;
    static constexpr layout::Rect kPathField{kBox.x + kPad, kBox.y + 12, kBox.w - 2 * kPad, 7};
    static constexpr layout::Rect kList{kBox.x + kPad, kBox.y + 22, kBox.w - 2 * kPad, 60};
    static constexpr float        kBottomY = kBox.y + kBox.h - kPad - 8; // the bottom row's top
    static constexpr layout::Rect kButton{kBox.x + kBox.w - kPad - 40, kBottomY, 40, 8};
    static constexpr layout::Rect kNameField{kBox.x + kPad + 16, kBottomY, kBox.w - 2 * kPad - 16 - 44, 8};
    static constexpr auto         kDoubleClick = std::chrono::milliseconds(350);

    /** How many entries fit in the list. */
    static int VisibleLines() { return int(kList.h / kLineHeight); }

    // InsertCard::Picker
    void Open(FolderBrowser::Mode mode, const std::filesystem::path& start, const std::string& name) override;
    void Close() override { open_ = false; }

    bool IsOpen() const { return open_; }

    /** A key went down. `ctrl` if Control is held. */
    Result Key(Scancode code, bool ctrl);
    /** Typed or pasted text. */
    Result Text(const std::string& text);
    /** A click with the left button. */
    Result Click(float x, float y, Clock::time_point now);
    /** The wheel over the list: `lines` up (negative) or down. */
    void Scroll(int lines);
    /** Call regularly: a single click opens its entry once it can't be a double-click. */
    Result Tick(Clock::time_point now);

    /** Shows why a choice was refused (InsertCard::Picked's answer), until the next input. */
    void SetError(std::string error) { error_ = std::move(error); }

    const FolderBrowser& Browser() const { return browser_; }
    const TextField&     PathField() const { return path_; }
    const TextField&     NameField() const { return name_; }
    Focus                GetFocus() const { return focus_; }
    /** The last error: a refused choice, a bad name, or a path that isn't there. */
    const std::string& Error() const { return error_; }
    int                FirstVisible() const { return first_visible_; }
    /** Entry `index`'s row in the list, wherever it scrolled. */
    layout::Rect EntryRect(int index) const;
    /** The title and the hint line for the mode and focus. */
    std::string Title() const;
    std::string Hint() const;
    std::string ButtonLabel() const;

  private:
    // The folder shown changed: the path field follows it, and the list scrolls to the selection.
    void Moved();
    void Reveal();
    void NextFocus();
    Result Choose();
    void OpenEntry(int index);

    FolderBrowser     browser_;
    TextField         path_;
    TextField         name_;
    Focus             focus_         = Focus::kList;
    std::string       error_;
    bool              open_          = false;
    int               first_visible_ = 0;
    int               pending_click_ = -1; // an entry clicked once, opened at Tick unless clicked again
    Clock::time_point clicked_at_{};
};

} // namespace champi
