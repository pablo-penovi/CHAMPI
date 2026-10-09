#include "connections_menu.h"

#include <algorithm>
#include <cctype>

namespace champi
{
namespace
{
bool Inside(const layout::Rect& r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// "playback_FL" and "playback_FR" as "playback_FL/FR": what they share, up to a separator, once.
std::string PairLabel(const std::string& l, const std::string& r)
{
    size_t common = 0;
    while(common < l.size() && common < r.size() && l[common] == r[common])
        common++;
    while(common > 0 && std::isalnum((unsigned char)l[common - 1]))
        common--;
    return l + "/" + r.substr(common);
}

ConnectionsMenu::Tick TickOf(const Graph& graph, const std::vector<std::pair<int, std::string>>& links)
{
    size_t on = 0;
    for(const auto& [champi, peer] : links)
        on += graph.Connected(champi, peer);
    return on == 0 ? ConnectionsMenu::Tick::kOff
           : on == links.size() ? ConnectionsMenu::Tick::kOn
                                : ConnectionsMenu::Tick::kSome;
}
} // namespace

layout::Rect ConnectionsMenu::RowRect(int column, int row)
{
    const float w = (kBox.w - 3 * kPad) / 2;
    return {kBox.x + kPad + column * (w + kPad), kBodyY + row * (kRowHeight + kRowGap), w, kRowHeight};
}

bool ConnectionsMenu::HasLevel(const Row& row)
{
    const ChampiPort& port = kChampiPorts[row.ports[0]];
    return port.input && port.type == PortType::kAudio;
}

layout::Rect ConnectionsMenu::LevelRect(int column, int row)
{
    const layout::Rect r = RowRect(column, row);
    return {r.x + r.w - 43, r.y + 8.2f, 40, 2.4f};
}

void ConnectionsMenu::SetRowLevel(const Row& row, int percent)
{
    for(int port : row.ports)
        levels_.percent[port] = std::clamp(percent, 0, InputLevels::kMax);
}

layout::Rect ConnectionsMenu::ItemRect(int item) const
{
    return {kList.x, kList.y + (item - scroll_) * kLineHeight, kList.w, kLineHeight};
}

void ConnectionsMenu::BuildRows()
{
    // Input and output rows: a pair is one row, or its two sides when split.
    const std::vector<int> layout_rows[2] = {{kMic, kLineL, kEventsIn}, {kMasterL, kPhonesL, kMidiOut}};
    for(int c = 0; c < 2; c++)
    {
        std::vector<Row> rows;
        auto add = [&](std::string label, std::vector<int> ports, int pair) {
            Row row;
            row.label = std::move(label);
            row.ports = std::move(ports);
            row.pair  = pair;
            rows.push_back(std::move(row));
        };
        for(int port : layout_rows[c])
        {
            int pair = -1;
            for(int p = 0; p < kNumPairs; p++)
                if(kPairs[p][0] == port)
                    pair = p;
            if(pair < 0)
                add(kChampiPorts[port].label, {port}, -1);
            else if(split_[pair])
                for(int side : kPairs[pair])
                    add(kChampiPorts[side].label, {side}, pair);
            else
            {
                std::string label = kChampiPorts[port].label;
                label.resize(label.size() - 2); // "Master L" -> "Master"
                add(label, {kPairs[pair][0], kPairs[pair][1]}, pair);
            }
        }
        // What each row is connected to, for the first level.
        for(Row& row : rows)
            for(const Item& item : BuildItems(row))
                if(item.kind == Item::Kind::kPeer && item.missing)
                    row.missing++;
                else if(item.kind == Item::Kind::kPeer && item.tick != Tick::kOff)
                    row.connected.push_back(item.client + ": " + item.label);
        columns_[c] = std::move(rows);
    }
    row_ = std::min(row_, int(columns_[column_].size()) - 1);
}

std::vector<ConnectionsMenu::Item> ConnectionsMenu::BuildItems(const Row& row) const
{
    const Graph&      graph = snapshot_.graph;
    std::vector<Item> items;
    if(row.pair >= 0)
    {
        Item split;
        split.kind  = Item::Kind::kSplit;
        split.label = "Route left and right separately";
        split.tick  = split_[row.pair] ? Tick::kOn : Tick::kOff;
        items.push_back(split);
    }

    // The ports that fit, by client, in the order the server lists them.
    std::vector<std::pair<std::string, std::vector<const PeerPort*>>> clients;
    for(const PeerPort& p : graph.ports)
    {
        if(!p.Fits(row.ports[0]))
            continue;
        const std::string client = p.Client();
        auto it = std::find_if(clients.begin(), clients.end(), [&](const auto& c) { return c.first == client; });
        if(it == clients.end())
            it = clients.insert(clients.end(), {client, {}});
        it->second.push_back(&p);
    }
    for(const auto& [client, ports] : clients)
    {
        Item header;
        header.label = client;
        items.push_back(header);
        for(size_t i = 0; i < ports.size(); i++)
        {
            Item item;
            item.kind   = Item::Kind::kPeer;
            item.client = client;
            if(row.ports.size() == 1)
            {
                item.label = ports[i]->Label();
                item.links = {{row.ports[0], ports[i]->name}};
            }
            else if(i + 1 < ports.size())
            {
                // Consecutive ports pair up, L to L and R to R...
                item.label = PairLabel(ports[i]->Label(), ports[i + 1]->Label());
                item.links = {{row.ports[0], ports[i]->name}, {row.ports[1], ports[i + 1]->name}};
                i++;
            }
            else
            {
                // ...and one left over is mono, so it gets both sides.
                item.label = ports[i]->Label() + " (both sides)";
                item.links = {{row.ports[0], ports[i]->name}, {row.ports[1], ports[i]->name}};
            }
            item.tick = TickOf(graph, item.links);
            items.push_back(std::move(item));
        }
    }

    // Saved peers that aren't there, one line per name even if both sides want it.
    std::vector<Item> missing;
    for(int champi : row.ports)
        for(const std::string& name : snapshot_.saved.peers[champi])
        {
            if(Resolve(graph, champi, name))
                continue;
            auto it = std::find_if(missing.begin(), missing.end(), [&](const Item& m) { return m.label == name; });
            if(it == missing.end())
            {
                Item item;
                item.kind    = Item::Kind::kPeer;
                item.label   = name;
                item.client  = name.substr(0, name.find(':'));
                item.tick    = Tick::kOn;
                item.missing = true;
                it           = missing.insert(missing.end(), item);
            }
            it->links.push_back({champi, name});
        }
    if(!missing.empty())
    {
        Item header;
        header.label = "Saved, not connected now";
        items.push_back(header);
        items.insert(items.end(), missing.begin(), missing.end());
    }
    return items;
}

void ConnectionsMenu::SetSnapshot(const RoutingSnapshot& snapshot)
{
    snapshot_ = snapshot;

    // A pair connected one side at a time can't be shown as one row.
    for(int p = 0; p < kNumPairs; p++)
    {
        if(split_[p])
            continue;
        Row pair;
        pair.ports = {kPairs[p][0], kPairs[p][1]};
        pair.pair  = p;
        for(const Item& item : BuildItems(pair))
            if(item.tick == Tick::kSome)
                split_[p] = true;
        if(split_[p] && in_peers_ && OpenRow().pair == p)
        {
            // The open row was this pair: it's now its left side.
            BuildRows();
            SelectPortRow(kPairs[p][0]);
        }
    }

    // Rebuild, keeping the selection on the same line if it's still there.
    const bool        had_item = in_peers_ && item_ < int(items_.size());
    const std::string label    = had_item ? items_[item_].label : "";
    const std::string client   = had_item ? items_[item_].client : "";
    BuildRows();
    if(!in_peers_)
        return;
    items_ = BuildItems(OpenRow());
    for(int i = 0; i < int(items_.size()); i++)
        if(items_[i].Selectable() && items_[i].label == label && items_[i].client == client)
        {
            item_ = i;
            EnsureVisible();
            return;
        }
    SelectNext(std::min(item_, int(items_.size()) - 1), 1);
}

void ConnectionsMenu::Reset()
{
    in_peers_ = false;
    items_.clear();
    BuildRows();
}

void ConnectionsMenu::SelectPortRow(int champi)
{
    for(int c = 0; c < 2; c++)
        for(int r = 0; r < int(columns_[c].size()); r++)
            if(columns_[c][r].ports[0] == champi)
            {
                column_ = c;
                row_    = r;
            }
}

void ConnectionsMenu::Open()
{
    in_peers_ = true;
    items_    = BuildItems(OpenRow());
    scroll_   = 0;
    item_     = 0;
    // Start on the first port, past the split line.
    for(int i = 0; i < int(items_.size()); i++)
        if(items_[i].kind == Item::Kind::kPeer)
        {
            item_ = i;
            break;
        }
    SelectNext(item_, 1);
}

void ConnectionsMenu::SelectNext(int from, int dir)
{
    // The nearest selectable line from `from` in `dir`, else the other way.
    for(int d : {dir, -dir})
        for(int i = from; i >= 0 && i < int(items_.size()); i += d)
            if(items_[i].Selectable())
            {
                item_ = i;
                EnsureVisible();
                return;
            }
    item_ = 0;
    EnsureVisible();
}

void ConnectionsMenu::EnsureVisible()
{
    const int lines = VisibleLines();
    // Show a client's header with its first port.
    int top = item_;
    if(top > 0 && !items_[top - 1].Selectable())
        top--;
    if(top < scroll_)
        scroll_ = top;
    if(item_ >= scroll_ + lines)
        scroll_ = item_ - lines + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(items_.size()) - lines));
}

void ConnectionsMenu::Move(int dx, int dy)
{
    if(!in_peers_)
    {
        const Row& row = columns_[column_][row_];
        if(dx && HasLevel(row))
            SetRowLevel(row, RowLevel(row) + dx * InputLevels::kStep);
        else if(dx)
            column_ = std::clamp(column_ + dx, 0, 1);
        row_ = std::clamp(row_ + dy, 0, int(columns_[column_].size()) - 1);
        return;
    }
    if(dy && item_ + dy >= 0 && item_ + dy < int(items_.size()))
    {
        // Only move if there's somewhere to go: past the last line, stay put.
        for(int i = item_ + dy; i >= 0 && i < int(items_.size()); i += dy)
            if(items_[i].Selectable())
            {
                item_ = i;
                break;
            }
        EnsureVisible();
    }
}

void ConnectionsMenu::SwitchColumn()
{
    if(in_peers_)
        return;
    column_ = 1 - column_;
    row_    = std::min(row_, int(columns_[column_].size()) - 1);
}

void ConnectionsMenu::Page(int dir)
{
    if(!in_peers_)
    {
        row_ = dir < 0 ? 0 : int(columns_[column_].size()) - 1;
        return;
    }
    SelectNext(std::clamp(item_ + dir * (VisibleLines() - 1), 0, std::max(0, int(items_.size()) - 1)), dir);
}

std::vector<RouteChange> ConnectionsMenu::Activate()
{
    if(!in_peers_)
    {
        Open();
        return {};
    }
    if(item_ >= int(items_.size()))
        return {};
    Item& item = items_[item_];
    if(item.kind == Item::Kind::kSplit)
    {
        const int pair = OpenRow().pair;
        split_[pair]   = !split_[pair];
        in_peers_      = false;
        BuildRows();
        SelectPortRow(kPairs[pair][0]);
        return {};
    }
    if(item.kind != Item::Kind::kPeer)
        return {};

    // Ticking connects every link, so a pair with one side on gets both; unticking disconnects.
    const bool               connect = item.tick != Tick::kOn;
    std::vector<RouteChange> changes;
    for(const auto& [champi, peer] : item.links)
        changes.push_back({champi, peer, connect});
    // Shown at once; the next snapshot confirms it.
    item.tick = connect ? Tick::kOn : Tick::kOff;
    return changes;
}

bool ConnectionsMenu::Back()
{
    if(!in_peers_)
        return false;
    in_peers_ = false;
    items_.clear();
    return true;
}

std::vector<RouteChange> ConnectionsMenu::Click(float x, float y)
{
    if(!in_peers_)
    {
        // The selected row's volume bar, and a little round it, sets the volume.
        const layout::Rect bar = LevelRect(column_, row_);
        if(HasLevel(OpenRow()) && x >= bar.x - 1 && x < bar.x + bar.w + 1 && y >= bar.y - 2.5f
           && y < bar.y + bar.h + 2.5f)
        {
            const float at = std::clamp((x - bar.x) / bar.w, 0.0f, 1.0f);
            SetRowLevel(OpenRow(), int(at * InputLevels::kMax + 0.5f));
            return {};
        }
        for(int c = 0; c < 2; c++)
            for(int r = 0; r < int(columns_[c].size()); r++)
                if(Inside(RowRect(c, r), x, y))
                {
                    column_ = c;
                    row_    = r;
                    Open();
                }
        return {};
    }
    if(Inside(kBack, x, y))
    {
        Back();
        return {};
    }
    if(!Inside(kList, x, y))
        return {};
    const int line = scroll_ + int((y - kList.y) / kLineHeight);
    if(line >= int(items_.size()) || !items_[line].Selectable())
        return {};
    item_ = line;
    return Activate();
}

void ConnectionsMenu::ScrollBy(int lines)
{
    if(!in_peers_)
        return;
    scroll_ = std::clamp(scroll_ + lines, 0, std::max(0, int(items_.size()) - VisibleLines()));
}

} // namespace champi
