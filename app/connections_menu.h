// The connections menu's logic: what it lists, where each thing sits, and what the mouse and keys do
// in it. The window draws it over the panel (champi_ui.cpp); none of it needs DPF.
//
// It has two levels. The first lists CHAMPI's ports in two columns, inputs and outputs, a stereo
// pair as one row unless it's split. Opening a row lists the ports it can connect to, grouped by
// client, each with a checkbox; a pair row ticks L to L and R to R, or both sides to a mono port.
// Saved peers that aren't there are listed last, greyed, and unticking one forgets it.
//
// Everything is in panel millimetres, like the panel it's drawn over.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "panel_layout.h"
#include "routing.h"

namespace champi
{
class ConnectionsMenu
{
  public:
    enum class Tick
    {
        kOff,
        kSome, // a pair item with one side connected
        kOn,
    };

    /** A row on the first level: one of CHAMPI's ports, or a stereo pair. */
    struct Row
    {
        std::string              label;
        std::vector<int>         ports; // CHAMPI ports, L then R for a pair
        int                      pair = -1; // which stereo pair it belongs to, split or not
        std::vector<std::string> connected; // "client: port" for each peer it's connected to
        int                      missing = 0; // saved peers that aren't there
    };

    /** A line on the second level. */
    struct Item
    {
        enum class Kind
        {
            kHeader, // a client's name
            kSplit,  // "route left and right separately", on a pair's rows
            kPeer,   // a port, or a pair of ports, to tick
        };
        Kind        kind = Kind::kHeader;
        std::string label;
        std::string client;
        std::vector<std::pair<int, std::string>> links; // CHAMPI port, peer
        Tick        tick    = Tick::kOff;
        bool        missing = false; // saved, but not on the graph

        bool Selectable() const { return kind != Kind::kHeader; }
    };

    // The stereo pairs: line in, master and phones.
    static constexpr int kNumPairs              = 3;
    static constexpr int kPairs[kNumPairs][2] = {{kLineL, kLineR}, {kMasterL, kMasterR}, {kPhonesL, kPhonesR}};

    // Where things are.
    static constexpr layout::Rect kBox{24, 5, 278, 100};
    static constexpr float        kPad        = 4;
    static constexpr float        kTitleY     = kBox.y + 7;   // the title's baseline centre
    static constexpr float        kSubtitleY  = kBox.y + 16;  // column headers, or the open row's name
    static constexpr float        kBodyY      = kBox.y + 21;  // where rows and the list start
    static constexpr float        kRowHeight  = 13;
    static constexpr float        kRowGap     = 2;
    static constexpr float        kLineHeight = 6;
    static constexpr layout::Rect kBack{kBox.x + kPad, kSubtitleY - 3, 16, 6};
    static constexpr layout::Rect kList{kBox.x + kPad, kBodyY, kBox.w - 2 * kPad, kBox.y + kBox.h - kPad - kBodyY};

    ConnectionsMenu() { BuildRows(); }

    /** Takes the latest graph and saved routing. */
    void SetSnapshot(const RoutingSnapshot& snapshot);
    /** Back to the first level, as it is when the menu opens. */
    void Reset();

    bool InPeers() const { return in_peers_; }

    /** The first level. Column 0 is the inputs, 1 the outputs. */
    const std::vector<Row>& Column(int column) const { return columns_[column]; }
    int SelectedColumn() const { return column_; }
    int SelectedRow() const { return row_; }
    static layout::Rect RowRect(int column, int row);

    /** The second level: the row that's open and its lines. */
    const Row&               OpenRow() const { return columns_[column_][row_]; }
    const std::vector<Item>& Items() const { return items_; }
    int                      SelectedItem() const { return item_; }
    int                      FirstVisible() const { return scroll_; }
    static int               VisibleLines() { return int(kList.h / kLineHeight); }
    /** Item i's line, wherever it is scrolled to; only lines in kList are drawn. */
    layout::Rect ItemRect(int item) const;

    // Keys. Each returns the connections to change, if any.
    void                     Move(int dx, int dy); // arrows
    void                     Page(int dir);        // page up and down
    std::vector<RouteChange> Activate();           // Enter or Space: open a row, or tick
    /** Esc: back a level. False if it was on the first level already, so the menu closes. */
    bool Back();

    // The mouse.
    std::vector<RouteChange> Click(float x, float y);
    void                     ScrollBy(int lines);

  private:
    void              BuildRows();
    std::vector<Item> BuildItems(const Row& row) const;
    void              Open();
    void              SelectNext(int from, int dir);
    void              EnsureVisible();
    void              SelectPortRow(int champi);

    RoutingSnapshot                  snapshot_;
    std::array<bool, kNumPairs>      split_{};
    std::array<std::vector<Row>, 2> columns_;
    int                              column_   = 0;
    int                              row_      = 0;
    bool                             in_peers_ = false;
    std::vector<Item>                items_;
    int                              item_   = 0;
    int                              scroll_ = 0;
};

} // namespace champi
