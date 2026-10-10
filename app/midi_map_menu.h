// The connections menu's MIDI controller mapping: a table of CHAMPI's controls and what the
// controller connected to MIDI in has mapped to each, where a control is learnt by pressing Enter
// on it and then working the controller's. The window draws it (champi_ui.cpp); none of it needs
// DPF. ConnectionsMenu opens it from the row under its inputs and outputs.
//
// It edits a copy of the mappings; the window saves the controllers TakeChanged names and plays
// the result (see ActiveMapping and MidiMapper). With more than one controller connected, Tab
// picks the one the table shows.
//
// Everything is in panel millimetres, like the panel it's drawn over.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "midi_map.h"
#include "panel_layout.h"
#include "routing.h"

namespace champi
{
class MidiMappingMenu
{
  public:
    // Where things are: in ConnectionsMenu's list area, a line of column headings, then the table.
    static layout::Rect Table();
    static float        HeadingY();
    static float        LineHeight();
    /** Where the second column, what each control is mapped to, starts. */
    static float BindingX();
    static int   VisibleLines();
    layout::Rect LineRect(int target) const;

    void                SetMappings(const MidiMappings& mappings) { mappings_ = mappings; }
    const MidiMappings& Mappings() const { return mappings_; }

    /** Takes the latest graph: which controllers are connected to MIDI in. */
    void SetGraph(const Graph& graph);

    /** The ports connected to MIDI in, and the one the table shows; empty if there's none. */
    const std::vector<std::string>& Controllers() const { return controllers_; }
    int                             SelectedController() const { return controller_; }
    std::string                     Controller() const;

    /** What the shown controller has target i mapped to. */
    MidiBinding Binding(int target) const;

    int Selected() const { return selected_; }
    int FirstVisible() const { return scroll_; }

    /** Whether it's waiting for the selected control to be worked on the controller. */
    bool Learning() const { return learning_; }
    /** A rotation's CCs heard so far, and from which controller. */
    int                Heard() const { return learner_.Heard(); }
    const MidiBinding& Hearing() const { return learner_.Hearing(); }

    /** Back to the top, not learning: as it is when it opens. */
    void Reset();

    // Keys.
    void Move(int dy);          // up and down
    void Page(int dir);         // page up and down
    void Activate();            // Enter: learn the selected control
    void Clear();               // Delete: unmap it
    void ChangeMode(int dir);   // left and right: how a knob's rotation reads its CC
    void SwitchController();    // Tab
    /** Esc: stops learning. False if it wasn't learning, so the menu goes back. */
    bool Back();

    // The mouse, on the table: a click learns the control it's on.
    void Click(float x, float y);
    void ScrollBy(int lines);

    /** A message heard while learning. Returns true once the control is learnt. */
    bool Feed(const uint8_t message[3]);

    /** The controllers whose mapping changed since the last call, by the name it's kept under. */
    std::vector<std::string> TakeChanged();

  private:
    void EnsureVisible();
    /** The name the shown controller's mapping is kept under. */
    std::string Key() const;

    MidiMappings             mappings_;
    Graph                    graph_;
    std::vector<std::string> controllers_;
    int                      controller_ = 0;
    int                      selected_   = 0;
    int                      scroll_     = 0;
    bool                     learning_   = false;
    MidiLearner              learner_;
    std::set<std::string>    changed_;
};

} // namespace champi
