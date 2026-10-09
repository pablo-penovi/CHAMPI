#include "routing.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "toml_lines.h"

namespace champi
{
const ChampiPort kChampiPorts[kNumChampiPorts] = {
    {"mic", "Mic", PortType::kAudio, true},
    {"line_l", "Line in L", PortType::kAudio, true},
    {"line_r", "Line in R", PortType::kAudio, true},
    {"events-in", "MIDI in", PortType::kMidi, true},
    {"master_l", "Master L", PortType::kAudio, false},
    {"master_r", "Master R", PortType::kAudio, false},
    {"phones_l", "Phones L", PortType::kAudio, false},
    {"phones_r", "Phones R", PortType::kAudio, false},
    {"midi-out", "MIDI out", PortType::kMidi, false},
};

int ChampiPortByName(std::string_view name)
{
    for(int i = 0; i < kNumChampiPorts; i++)
        if(name == kChampiPorts[i].name)
            return i;
    return -1;
}

std::string PeerPort::Client() const
{
    return name.substr(0, name.find(':'));
}

std::string PeerPort::Label() const
{
    if(!pretty.empty())
        return pretty;
    const size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

bool PeerPort::Fits(int champi) const
{
    return type == kChampiPorts[champi].type && output == kChampiPorts[champi].input;
}

const PeerPort* Graph::Find(std::string_view name) const
{
    for(const PeerPort& p : ports)
        if(p.name == name)
            return &p;
    return nullptr;
}

namespace
{
// The name without the "-<number>" PipeWire adds to a client whose name is taken, as in
// "MiniFuse 1 Loopback L/R-62:capture_FL".
std::string WithoutClientSuffix(const std::string& name)
{
    const size_t colon = name.find(':');
    if(colon == std::string::npos)
        return name;
    size_t start = colon;
    while(start > 0 && std::isdigit((unsigned char)name[start - 1]))
        start--;
    if(start == colon || start == 0 || name[start - 1] != '-')
        return name;
    return name.substr(0, start - 1) + name.substr(colon);
}
} // namespace

const PeerPort* Resolve(const Graph& graph, int champi, const std::string& saved)
{
    const PeerPort* found = graph.Find(saved);
    if(found)
        return found->Fits(champi) ? found : nullptr;
    for(const PeerPort& p : graph.ports)
        if(p.Fits(champi) && std::find(p.aliases.begin(), p.aliases.end(), saved) != p.aliases.end())
            return &p;
    const std::string base = WithoutClientSuffix(saved);
    for(const PeerPort& p : graph.ports)
        if(p.Fits(champi) && WithoutClientSuffix(p.name) == base)
            return &p;
    return nullptr;
}

SavedRouting SavedRouting::Parse(std::string_view toml, const std::string& source)
{
    SavedRouting          routing;
    std::set<std::string> seen;
    const auto lines = ParseTomlLines(toml, source, "a port name in quotes",
                                      "tables aren't used in connections.toml; write `port = [\"client:port\"]` lines");
    for(const TomlLine& l : lines)
    {
        const int champi = ChampiPortByName(l.name);
        if(champi < 0)
            TomlFail(source, l.line, "CHAMPI has no port called \"" + l.name + "\"");
        if(!seen.insert(l.name).second)
            TomlFail(source, l.line, l.name + " is set twice");
        auto& peers = routing.peers[champi];
        for(const TomlValue& v : l.values)
        {
            if(!v.is_string)
                TomlFail(source, v.line, "port names go in quotes");
            if(v.text.find(':') == std::string::npos)
                TomlFail(source, v.line, "\"" + v.text + "\" isn't a port: write \"client:port\"");
            if(std::find(peers.begin(), peers.end(), v.text) == peers.end())
                peers.push_back(v.text);
        }
    }
    return routing;
}

std::optional<SavedRouting> SavedRouting::Load(const std::filesystem::path& path)
{
    std::error_code ec;
    if(!std::filesystem::exists(path, ec))
        return std::nullopt;
    std::ifstream in(path);
    if(!in)
        throw std::runtime_error("can't read " + path.string());
    std::stringstream text;
    text << in.rdbuf();
    return Parse(text.str(), path.string());
}

std::string SavedRouting::ToToml() const
{
    std::string out = "# CHAMPI's connections, written by the connections menu (F8). Each line lists\n"
                      "# the JACK/PipeWire ports a CHAMPI port is connected to at start, and again\n"
                      "# when one of them appears.\n\n";
    for(int i = 0; i < kNumChampiPorts; i++)
    {
        out += std::string(kChampiPorts[i].name) + " = [";
        for(size_t k = 0; k < peers[i].size(); k++)
            out += (k ? ", " : "") + TomlQuote(peers[i][k]);
        out += "]\n";
    }
    return out;
}

void SavedRouting::Save(const std::filesystem::path& path) const
{
    if(path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << ToToml();
        if(!out.flush())
            throw std::runtime_error("can't write " + tmp.string());
    }
    std::filesystem::rename(tmp, path);
}

Router::Router(RoutingBackend& backend, std::optional<SavedRouting> saved, bool auto_connect, SaveFn save)
    : backend_(backend), saved_(saved.value_or(SavedRouting{})), defaults_(!saved),
      auto_connect_(auto_connect), save_(std::move(save))
{
}

void Router::Update()
{
    graph_ = backend_.List();
    if(!graph_.champi_present)
        return;

    std::set<std::string> now, appeared;
    for(const PeerPort& p : graph_.ports)
    {
        now.insert(p.name);
        if(!seen_.count(p.name))
            appeared.insert(p.name);
    }
    const bool first = !started_;
    started_         = true;
    seen_            = std::move(now);
    if(!auto_connect_)
        return;

    if(defaults_)
    {
        if(first)
        {
            int master = kMasterL;
            for(const PeerPort& p : graph_.ports)
                if(master <= kMasterR && p.physical && p.Fits(kMasterL))
                    Want(master++, p.name);
        }
        for(const PeerPort& p : graph_.ports)
            if(appeared.count(p.name) && p.physical && p.Fits(kEventsIn) &&
               p.name.find("Midi Through") == std::string::npos)
                Want(kEventsIn, p.name);
    }
    Restore(appeared);
}

void Router::Restore(const std::set<std::string>& appeared)
{
    bool made = false;
    for(int c = 0; c < kNumChampiPorts; c++)
        for(const std::string& name : saved_.peers[c])
        {
            const PeerPort* p = Resolve(graph_, c, name);
            if(p && appeared.count(p->name) && !graph_.Connected(c, p->name))
                made |= backend_.Connect(c, p->name);
        }
    if(made)
        graph_ = backend_.List();
}

void Router::Want(int champi, const std::string& peer)
{
    auto& peers = saved_.peers[champi];
    for(const std::string& s : peers)
    {
        const PeerPort* p = Resolve(graph_, champi, s);
        if(s == peer || (p && p->name == peer))
            return;
    }
    peers.push_back(peer);
}

void Router::Apply(const std::vector<RouteChange>& changes)
{
    for(const RouteChange& ch : changes)
    {
        const PeerPort* p         = graph_.Find(ch.peer);
        const bool      connected = p && graph_.Connected(ch.champi, ch.peer);
        if(ch.connect)
        {
            if(p && p->Fits(ch.champi) && !connected)
                backend_.Connect(ch.champi, ch.peer);
            Want(ch.champi, ch.peer);
            continue;
        }
        if(connected)
            backend_.Disconnect(ch.champi, ch.peer);
        auto& peers = saved_.peers[ch.champi];
        peers.erase(std::remove_if(peers.begin(), peers.end(),
                                   [&](const std::string& s) {
                                       const PeerPort* r = Resolve(graph_, ch.champi, s);
                                       return s == ch.peer || (r && r->name == ch.peer);
                                   }),
                    peers.end());
    }
    defaults_ = false;
    if(save_)
        save_(saved_);
    graph_ = backend_.List();
}

} // namespace champi
