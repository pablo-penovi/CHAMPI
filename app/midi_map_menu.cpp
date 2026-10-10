#include "midi_map_menu.h"

#include <algorithm>

#include "connections_menu.h"

namespace champi
{
namespace
{
bool IsTurn(int target)
{
    return MidiTargetAt(target).kind == MidiTarget::Kind::kKnobTurn;
}
} // namespace

layout::Rect MidiMappingMenu::Table()
{
    const layout::Rect& list = ConnectionsMenu::kList;
    return {list.x, list.y + LineHeight(), list.w, list.h - LineHeight()};
}

float MidiMappingMenu::HeadingY()
{
    return ConnectionsMenu::kList.y + LineHeight() / 2;
}

float MidiMappingMenu::LineHeight()
{
    return ConnectionsMenu::kLineHeight;
}

float MidiMappingMenu::BindingX()
{
    return Table().x + 78;
}

int MidiMappingMenu::VisibleLines()
{
    return int(Table().h / LineHeight());
}

layout::Rect MidiMappingMenu::LineRect(int target) const
{
    const layout::Rect t = Table();
    return {t.x, t.y + (target - scroll_) * LineHeight(), t.w, LineHeight()};
}

void MidiMappingMenu::SetGraph(const Graph& graph)
{
    const std::string shown = Controller();
    graph_                  = graph;
    controllers_            = MidiControllers(graph);
    // Stay on the controller that was shown, if it's still there.
    const auto it = std::find(controllers_.begin(), controllers_.end(), shown);
    controller_   = it != controllers_.end() ? int(it - controllers_.begin()) : 0;
    if(controllers_.empty() || it == controllers_.end())
        learning_ = false;
}

std::string MidiMappingMenu::Controller() const
{
    return controllers_.empty() ? "" : controllers_[controller_];
}

std::string MidiMappingMenu::Key() const
{
    return mappings_.KeyForPort(graph_, Controller());
}

MidiBinding MidiMappingMenu::Binding(int target) const
{
    if(controllers_.empty())
        return {};
    const MidiProfile* profile = mappings_.ForPort(graph_, Controller());
    return profile ? profile->bindings[target] : MidiBinding{};
}

void MidiMappingMenu::Reset()
{
    learning_ = false;
    selected_ = 0;
    scroll_   = 0;
}

void MidiMappingMenu::EnsureVisible()
{
    const int lines = VisibleLines();
    if(selected_ < scroll_)
        scroll_ = selected_;
    if(selected_ >= scroll_ + lines)
        scroll_ = selected_ - lines + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, kNumMidiTargets - lines));
}

void MidiMappingMenu::Move(int dy)
{
    learning_ = false;
    selected_ = std::clamp(selected_ + dy, 0, kNumMidiTargets - 1);
    EnsureVisible();
}

void MidiMappingMenu::Page(int dir)
{
    Move(dir * (VisibleLines() - 1));
}

void MidiMappingMenu::Activate()
{
    if(controllers_.empty())
        return;
    learning_ = true;
    learner_  = MidiLearner(IsTurn(selected_));
}

void MidiMappingMenu::Clear()
{
    learning_ = false;
    if(!Binding(selected_).Bound())
        return;
    mappings_.Get(Key()).Bind(selected_, {});
    changed_.insert(Key());
}

void MidiMappingMenu::ChangeMode(int dir)
{
    MidiBinding b = Binding(selected_);
    if(learning_ || !IsTurn(selected_) || !b.Bound())
        return;
    b.mode = KnobMode(((int(b.mode) + dir) % kNumKnobModes + kNumKnobModes) % kNumKnobModes);
    mappings_.Get(Key()).Bind(selected_, b);
    changed_.insert(Key());
}

void MidiMappingMenu::SwitchController()
{
    if(controllers_.size() < 2)
        return;
    learning_   = false;
    controller_ = (controller_ + 1) % int(controllers_.size());
}

bool MidiMappingMenu::Back()
{
    if(!learning_)
        return false;
    learning_ = false;
    return true;
}

void MidiMappingMenu::Click(float x, float y)
{
    const layout::Rect t = Table();
    if(x < t.x || x >= t.x + t.w || y < t.y || y >= t.y + t.h)
        return;
    const int line = scroll_ + int((y - t.y) / LineHeight());
    if(line >= kNumMidiTargets)
        return;
    selected_ = line;
    EnsureVisible();
    Activate();
}

void MidiMappingMenu::ScrollBy(int lines)
{
    scroll_ = std::clamp(scroll_ + lines, 0, std::max(0, kNumMidiTargets - VisibleLines()));
}

bool MidiMappingMenu::Feed(const uint8_t message[3])
{
    if(!learning_)
        return false;
    const auto binding = learner_.Feed(message);
    if(!binding)
        return false;
    learning_ = false;
    mappings_.Get(Key()).Bind(selected_, *binding);
    changed_.insert(Key());
    return true;
}

std::vector<std::string> MidiMappingMenu::TakeChanged()
{
    std::vector<std::string> changed(changed_.begin(), changed_.end());
    changed_.clear();
    return changed;
}

} // namespace champi
