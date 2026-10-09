// Where CHAMPI's ports are connected: its nine JACK ports, the other ports on the graph they can
// connect to, the routing saved in connections.toml, and Router, which keeps the two in step.
//
// No JACK here: a RoutingBackend lists the graph and makes the connections. JackMonitor provides
// the real one; the tests use a fake.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace champi
{
enum class PortType
{
    kAudio,
    kMidi,
};

/** One of CHAMPI's own ports. */
struct ChampiPort
{
    const char* name;  // the JACK port name, after "CHAMPI:", and its key in connections.toml
    const char* label; // in the menu
    PortType    type;
    bool        input; // CHAMPI receives on it
};

constexpr int kNumChampiPorts = 9;
enum ChampiPortIndex
{
    kMic,
    kLineL,
    kLineR,
    kEventsIn,
    kMasterL,
    kMasterR,
    kPhonesL,
    kPhonesR,
    kMidiOut,
};
extern const ChampiPort kChampiPorts[kNumChampiPorts];

/** The index of the CHAMPI port called `name`, or -1. */
int ChampiPortByName(std::string_view name);

/** A port on the graph that isn't CHAMPI's. */
struct PeerPort
{
    std::string              name;    // "client:port"
    std::string              pretty;  // its JACK pretty-name, if it has one
    std::vector<std::string> aliases; // JACK's port aliases
    PortType                 type   = PortType::kAudio;
    bool                     output = false; // it sends: CHAMPI's inputs connect from it
    bool                     physical = false;

    /** The part before the first ':'. */
    std::string Client() const;
    /** The readable name: the pretty-name, else the part after the client. */
    std::string Label() const;
    /** Whether CHAMPI's port `champi` can connect to it. */
    bool Fits(int champi) const;
};

/** What a backend sees: the other ports and CHAMPI's connections to them. */
struct Graph
{
    bool                                   champi_present = false; // CHAMPI's ports exist
    std::vector<PeerPort>                  ports;                  // in the server's order
    std::set<std::pair<int, std::string>> connections;            // CHAMPI port, peer name

    const PeerPort* Find(std::string_view name) const;
    bool Connected(int champi, const std::string& peer) const { return connections.count({champi, peer}) > 0; }
};

/** Lists the graph and connects CHAMPI's ports; JACK in the app, a fake in the tests. */
class RoutingBackend
{
  public:
    virtual ~RoutingBackend() = default;
    virtual Graph List() = 0;
    /** Connects or disconnects CHAMPI's port to the peer, whichever way round they go. */
    virtual bool Connect(int champi, const std::string& peer)    = 0;
    virtual bool Disconnect(int champi, const std::string& peer) = 0;
};

/**
 * The routing kept in connections.toml: for each CHAMPI port, the peers it should be connected
 * to, by name.
 *
 *     master_l = ["MiniFuse 1 Main Output L/R:playback_FL"]
 *     events-in = ["Midi-Bridge:KeyStep 32 (capture)", "BLE MIDI 1:out"]
 *     mic = []
 *
 * Ports the file doesn't name have no saved peers.
 */
struct SavedRouting
{
    std::array<std::vector<std::string>, kNumChampiPorts> peers;

    /** Parses connections.toml. Throws std::runtime_error with the line on anything it doesn't
     *  understand. */
    static SavedRouting Parse(std::string_view toml, const std::string& source = "connections");
    /** Reads a file; empty if it doesn't exist. Throws as Parse, or if it can't be read. */
    static std::optional<SavedRouting> Load(const std::filesystem::path& path);

    std::string ToToml() const;
    /** Writes the file, creating its directory, by way of a temporary file. Throws on failure. */
    void Save(const std::filesystem::path& path) const;

    bool operator==(const SavedRouting& o) const { return peers == o.peers; }
};

/** The present port a saved peer name stands for, if any, that `champi` can connect to: the port
 *  of that name, else one with it as an alias, else one whose name is the same but for PipeWire's
 *  "-<number>" suffix on the client, which changes between sessions. */
const PeerPort* Resolve(const Graph& graph, int champi, const std::string& saved);

/** One change the menu asks for. */
struct RouteChange
{
    int         champi;
    std::string peer; // a present port's name, or a saved name that's missing
    bool        connect;
};

/**
 * Keeps CHAMPI's connections in step with the saved routing.
 *
 * - Update() lists the graph. The first time CHAMPI's ports are there, it makes every saved
 *   connection; after that, it makes the saved connections of each peer that appears, so a device
 *   plugged in later gets connected. A saved connection undone elsewhere (qpwgraph, pw-link) stays
 *   undone until its peer comes back.
 * - Without a saved file it uses the defaults: master out to the first two physical playback ports,
 *   at start, and every physical MIDI source except the kernel's Midi Through to events-in, at
 *   start and whenever one appears. What they connect counts as saved from then on.
 * - Apply() makes the menu's changes and saves the routing, the menu's changes included. Only the
 *   menu's changes are saved: connections made elsewhere aren't.
 * - Without `auto_connect` (--no-connect) nothing is connected on its own, but the menu still
 *   works and saves.
 */
class Router
{
  public:
    using SaveFn = std::function<void(const SavedRouting&)>;

    Router(RoutingBackend& backend, std::optional<SavedRouting> saved, bool auto_connect, SaveFn save);

    void Update();
    void Apply(const std::vector<RouteChange>& changes);

    const Graph&        graph() const { return graph_; }
    const SavedRouting& saved() const { return saved_; }

  private:
    void Want(int champi, const std::string& peer);
    void Restore(const std::set<std::string>& appeared);

    RoutingBackend&       backend_;
    SavedRouting          saved_;
    bool                  defaults_; // no file yet: the defaults apply
    bool                  auto_connect_;
    SaveFn                save_;
    Graph                 graph_;
    bool                  started_ = false; // CHAMPI's ports have been seen
    std::set<std::string> seen_;            // the peers there at the last update
};

/** What the menu draws from: the graph and the saved routing. */
struct RoutingSnapshot
{
    Graph        graph;
    SavedRouting saved;
    uint64_t     version = 0; // goes up with every change
};

/** The menu's side of the routing: it reads snapshots and sends changes. JackMonitor runs one. */
class RoutingService
{
  public:
    virtual ~RoutingService() = default;
    /** The latest snapshot, if its version isn't `have`. */
    virtual std::optional<RoutingSnapshot> SnapshotIfNewer(uint64_t have) = 0;
    virtual void Request(std::vector<RouteChange> changes) = 0;
};

} // namespace champi
