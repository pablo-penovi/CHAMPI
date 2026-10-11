#include "folder_picker.h"

#include <linux/input-event-codes.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace champi
{
namespace
{
bool Inside(const layout::Rect& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
} // namespace

void FolderPicker::Open(FolderBrowser::Mode mode, const fs::path& start, const std::string& name)
{
    browser_.Open(mode, start);
    name_.Set(name);
    focus_         = Focus::kList;
    error_.clear();
    open_          = true;
    pending_click_ = -1;
    first_visible_ = 0;
    Moved();
}

void FolderPicker::Moved()
{
    path_.Set(browser_.Path().string());
    pending_click_ = -1;
    if(!browser_.Error().empty())
        error_.clear(); // the list shows the browser's own error
    Reveal();
}

void FolderPicker::Reveal()
{
    const int selected = browser_.Selected();
    const int lines    = VisibleLines();
    if(selected < first_visible_)
        first_visible_ = std::max(selected, 0);
    else if(selected >= first_visible_ + lines)
        first_visible_ = selected - lines + 1;
    first_visible_ = std::clamp(first_visible_, 0, std::max(0, int(browser_.Entries().size()) - lines));
}

layout::Rect FolderPicker::EntryRect(int index) const
{
    return {kList.x, kList.y + (index - first_visible_) * kLineHeight, kList.w, kLineHeight};
}

void FolderPicker::NextFocus()
{
    switch(focus_)
    {
        case Focus::kList: focus_ = Focus::kPath; break;
        case Focus::kPath:
            focus_ = browser_.GetMode() == FolderBrowser::Mode::kCreate ? Focus::kName : Focus::kButton;
            break;
        case Focus::kName: focus_ = Focus::kButton; break;
        case Focus::kButton: focus_ = Focus::kList; break;
    }
    if(focus_ == Focus::kPath)
        path_.End();
}

FolderPicker::Result FolderPicker::Choose()
{
    if(browser_.GetMode() == FolderBrowser::Mode::kSelect)
        return {Result::Kind::kChosen, browser_.Path()};
    std::string    error;
    const fs::path dir = browser_.NewFolder(name_.Text(), error);
    if(dir.empty())
    {
        error_ = error;
        focus_ = Focus::kName;
        return {};
    }
    return {Result::Kind::kChosen, dir};
}

void FolderPicker::OpenEntry(int index)
{
    browser_.Open(index);
    Moved();
}

FolderPicker::Result FolderPicker::Key(Scancode code, bool ctrl)
{
    if(!open_)
        return {};
    if(code == KEY_ESC)
        return {Result::Kind::kCancelled, {}};
    if(code == KEY_TAB)
    {
        NextFocus();
        return {};
    }
    if(ctrl && (code == KEY_ENTER || code == KEY_KPENTER))
        return Choose();
    error_.clear();

    TextField* field = focus_ == Focus::kPath ? &path_ : focus_ == Focus::kName ? &name_ : nullptr;
    if(field)
    {
        switch(code)
        {
            case KEY_BACKSPACE: field->Backspace(); break;
            case KEY_DELETE: field->Delete(); break;
            case KEY_LEFT: field->Left(); break;
            case KEY_RIGHT: field->Right(); break;
            case KEY_HOME: field->Home(); break;
            case KEY_END: field->End(); break;
            case KEY_UP:
            case KEY_DOWN:
                focus_ = Focus::kList;
                path_.Set(browser_.Path().string());
                browser_.Move(code == KEY_UP ? -1 : 1);
                Reveal();
                break;
            case KEY_ENTER:
            case KEY_KPENTER:
                if(field == &name_)
                    return Choose();
                if(browser_.GoTo(path_.Text()))
                {
                    focus_ = Focus::kList;
                    Moved();
                }
                else
                    error_ = browser_.Error();
                break;
            default: break;
        }
        return {};
    }

    switch(code)
    {
        case KEY_UP: browser_.Move(-1); break;
        case KEY_DOWN: browser_.Move(1); break;
        case KEY_PAGEUP: browser_.Move(-VisibleLines()); break;
        case KEY_PAGEDOWN: browser_.Move(VisibleLines()); break;
        case KEY_HOME: browser_.Select(0); break;
        case KEY_END: browser_.Select(int(browser_.Entries().size()) - 1); break;
        case KEY_BACKSPACE:
            browser_.Up();
            Moved();
            break;
        case KEY_ENTER:
        case KEY_KPENTER:
        case KEY_SPACE:
            if(focus_ == Focus::kButton)
                return Choose();
            if(code != KEY_SPACE)
                OpenEntry(browser_.Selected());
            break;
        default: break;
    }
    Reveal();
    return {};
}

FolderPicker::Result FolderPicker::Text(const std::string& text)
{
    if(!open_ || text.empty() || (unsigned char)text[0] < 0x20 || text == "\x7f")
        return {};
    // Typing from the list or the button goes to the field it most likely means.
    if(focus_ == Focus::kList || focus_ == Focus::kButton)
    {
        if(text == " ")
            return {};
        focus_ = browser_.GetMode() == FolderBrowser::Mode::kCreate ? Focus::kName : Focus::kPath;
        if(focus_ == Focus::kPath)
            path_.Set(text == "/" || text == "~" ? "" : browser_.Path().string() + "/");
        else
            name_.Set("");
    }
    error_.clear();
    (focus_ == Focus::kPath ? path_ : name_).Insert(text);
    return {};
}

FolderPicker::Result FolderPicker::Click(float x, float y, Clock::time_point now)
{
    if(!open_)
        return {};
    error_.clear();
    if(!Inside(kBox, x, y))
        return {Result::Kind::kCancelled, {}};
    if(Inside(kPathField, x, y))
    {
        focus_ = Focus::kPath;
        path_.End();
        return {};
    }
    if(browser_.GetMode() == FolderBrowser::Mode::kCreate && Inside(kNameField, x, y))
    {
        focus_ = Focus::kName;
        name_.End();
        return {};
    }
    if(Inside(kButton, x, y))
    {
        focus_ = Focus::kButton;
        return Choose();
    }
    if(Inside(kList, x, y))
    {
        focus_      = Focus::kList;
        const int i = first_visible_ + int((y - kList.y) / kLineHeight);
        if(i < 0 || i >= int(browser_.Entries().size()))
            return {};
        if(pending_click_ == i && now - clicked_at_ <= kDoubleClick)
        {
            pending_click_ = -1;
            // A double-click chooses in select mode; ".." and create mode have nothing to choose.
            if(browser_.GetMode() == FolderBrowser::Mode::kSelect && !browser_.Entries()[i].up)
                return {Result::Kind::kChosen, browser_.EntryPath(i)};
            OpenEntry(i);
            return {};
        }
        browser_.Select(i);
        pending_click_ = i;
        clicked_at_    = now;
    }
    return {};
}

FolderPicker::Result FolderPicker::Tick(Clock::time_point now)
{
    if(open_ && pending_click_ >= 0 && now - clicked_at_ > kDoubleClick)
    {
        const int i    = pending_click_;
        pending_click_ = -1;
        OpenEntry(i);
    }
    return {};
}

void FolderPicker::Scroll(int lines)
{
    const int max  = std::max(0, int(browser_.Entries().size()) - VisibleLines());
    first_visible_ = std::clamp(first_visible_ + lines, 0, max);
}

std::string FolderPicker::Title() const
{
    return browser_.GetMode() == FolderBrowser::Mode::kCreate ? "Create a new card" : "Select a card folder";
}

std::string FolderPicker::ButtonLabel() const
{
    return browser_.GetMode() == FolderBrowser::Mode::kCreate ? "Create" : "Use this folder";
}

std::string FolderPicker::Hint() const
{
    switch(focus_)
    {
        case Focus::kPath: return "Enter goes there    Tab: next    Esc cancels";
        case Focus::kName: return "Enter creates it    Tab: next    Esc cancels";
        case Focus::kButton: return "Enter: " + ButtonLabel() + "    Tab: next    Esc cancels";
        case Focus::kList: break;
    }
    return std::string("Enter opens    Backspace goes up    Ctrl+Enter: ") + ButtonLabel() + "    Esc cancels";
}

} // namespace champi
