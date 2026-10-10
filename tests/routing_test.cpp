// The connections: the routing model against a fake JACK, connections.toml, audio_levels.toml,
// and the connections menu's navigation and hit-testing.
#include <algorithm>
#include <stdexcept>
#include <gtest/gtest.h>

#include "connections_menu.h"
#include "routing.h"
#include "test_util.h"

using champi::ConnectionsMenu;
using champi::Graph;
using champi::PeerPort;
using champi::PortType;
using champi::RouteChange;
using champi::Router;
using champi::SavedRouting;
using Tick = ConnectionsMenu::Tick;
using Kind = ConnectionsMenu::Item::Kind;

namespace
{
// The graph as a JACK server would show it, without one.
class FakeJack : public champi::RoutingBackend
{
  public:
    bool                                   champi = true; // CHAMPI's ports exist
    std::vector<PeerPort>                  ports;
    std::set<std::pair<int, std::string>> connections;
    int                                    connects = 0;

    Graph List() override
    {
        Graph g;
        g.champi_present = champi;
        g.ports          = ports;
        if(champi)
            g.connections = connections;
        return g;
    }

    bool Connect(int c, const std::string& peer) override
    {
        if(!Has(peer))
            return false;
        connects++;
        connections.insert({c, peer});
        return true;
    }

    bool Disconnect(int c, const std::string& peer) override { return connections.erase({c, peer}) > 0; }

    void Add(const std::string& name, PortType type, bool output, bool physical = false,
             std::vector<std::string> aliases = {})
    {
        PeerPort p;
        p.name     = name;
        p.type     = type;
        p.output   = output;
        p.physical = physical;
        p.aliases  = std::move(aliases);
        ports.push_back(p);
    }

    void Remove(const std::string& name)
    {
        ports.erase(std::remove_if(ports.begin(), ports.end(), [&](const PeerPort& p) { return p.name == name; }),
                    ports.end());
        for(auto it = connections.begin(); it != connections.end();)
            it = it->second == name ? connections.erase(it) : std::next(it);
    }

    bool Has(const std::string& name) const
    {
        return std::any_of(ports.begin(), ports.end(), [&](const PeerPort& p) { return p.name == name; });
    }

    bool Connected(int c, const std::string& peer) const { return connections.count({c, peer}) > 0; }

    // A typical PipeWire graph: a sound card, a second one, and MIDI.
    void Typical()
    {
        Add("Card:playback_FL", PortType::kAudio, false, true);
        Add("Card:playback_FR", PortType::kAudio, false, true);
        Add("Card:capture_FL", PortType::kAudio, true, true);
        Add("Card:capture_FR", PortType::kAudio, true, true);
        Add("Other:playback_FL", PortType::kAudio, false, true);
        Add("Other:playback_FR", PortType::kAudio, false, true);
        Add("Midi-Bridge:Midi Through: Port-0 (capture)", PortType::kMidi, true, true);
        Add("Midi-Bridge:Midi Through: Port-0 (playback)", PortType::kMidi, false, true);
        Add("Midi-Bridge:KeyStep (capture)", PortType::kMidi, true, true);
        Add("Synth:midi_in", PortType::kMidi, false, false);
        Add("Looper:out", PortType::kMidi, true, false);
    }
};

// A Router whose saves are counted and kept.
struct Saving
{
    int                         saves = 0;
    std::optional<SavedRouting> last;
    Router::SaveFn              Fn()
    {
        return [this](const SavedRouting& r) {
            saves++;
            last = r;
        };
    }
};

SavedRouting Routing(std::initializer_list<std::pair<int, std::vector<std::string>>> peers)
{
    SavedRouting r;
    for(const auto& [port, names] : peers)
        r.peers[port] = names;
    return r;
}

std::string ParseError(const std::string& toml)
{
    try
    {
        SavedRouting::Parse(toml, "connections.toml");
    }
    catch(const std::runtime_error& e)
    {
        return e.what();
    }
    return "";
}
} // namespace

// ---- The routing model --------------------------------------------------------------------------

TEST(Router, WithoutAFileTheDefaultsApplyOnceChampisPortsExist)
{
    FakeJack jack;
    jack.Typical();
    jack.champi = false;
    Saving saving;
    Router router(jack, std::nullopt, true, saving.Fn());

    router.Update();
    EXPECT_TRUE(jack.connections.empty());

    jack.champi = true;
    router.Update();
    // Master to the first two physical playback ports, physical MIDI sources but Midi Through
    // to events-in. Nothing else.
    EXPECT_EQ(jack.connections, (std::set<std::pair<int, std::string>>{
                                    {champi::kMasterL, "Card:playback_FL"},
                                    {champi::kMasterR, "Card:playback_FR"},
                                    {champi::kEventsIn, "Midi-Bridge:KeyStep (capture)"},
                                }));
    EXPECT_EQ(router.graph().connections, jack.connections);
    EXPECT_EQ(saving.saves, 0); // only the menu writes the file
}

TEST(Router, WithoutAFileAControllerPluggedInLaterIsConnected)
{
    FakeJack jack;
    jack.Typical();
    Router router(jack, std::nullopt, true, nullptr);
    router.Update();

    jack.Add("Midi-Bridge:Launchkey (capture)", PortType::kMidi, true, true);
    jack.Add("Audio:playback_1", PortType::kAudio, false, true);
    router.Update();
    EXPECT_TRUE(jack.Connected(champi::kEventsIn, "Midi-Bridge:Launchkey (capture)"));
    // Master stays where it went at start.
    EXPECT_FALSE(jack.Connected(champi::kMasterL, "Audio:playback_1"));
}

TEST(Router, TheSavedRoutingIsRestoredAtStartInsteadOfTheDefaults)
{
    FakeJack jack;
    jack.Typical();
    Router router(jack, Routing({{champi::kMasterL, {"Other:playback_FL"}}, {champi::kLineL, {"Card:capture_FL"}}}),
                  true, nullptr);
    router.Update();
    EXPECT_EQ(jack.connections, (std::set<std::pair<int, std::string>>{
                                    {champi::kMasterL, "Other:playback_FL"},
                                    {champi::kLineL, "Card:capture_FL"},
                                }));
}

TEST(Router, ASavedPeerIsConnectedWhenItAppears)
{
    FakeJack jack;
    jack.Typical();
    Router router(jack, Routing({{champi::kEventsIn, {"Midi-Bridge:Launchkey (capture)"}}}), true, nullptr);
    router.Update();
    EXPECT_TRUE(jack.connections.empty());

    jack.Add("Midi-Bridge:Launchkey (capture)", PortType::kMidi, true, true);
    router.Update();
    EXPECT_TRUE(jack.Connected(champi::kEventsIn, "Midi-Bridge:Launchkey (capture)"));

    // Unplugged and plugged in again, it's connected again.
    jack.Remove("Midi-Bridge:Launchkey (capture)");
    router.Update();
    jack.Add("Midi-Bridge:Launchkey (capture)", PortType::kMidi, true, true);
    router.Update();
    EXPECT_TRUE(jack.Connected(champi::kEventsIn, "Midi-Bridge:Launchkey (capture)"));
}

TEST(Router, ASavedConnectionUndoneElsewhereStaysUndone)
{
    FakeJack jack;
    jack.Typical();
    Router router(jack, Routing({{champi::kMasterL, {"Card:playback_FL"}}}), true, nullptr);
    router.Update();
    jack.Disconnect(champi::kMasterL, "Card:playback_FL"); // in qpwgraph
    router.Update();
    router.Update();
    EXPECT_FALSE(jack.Connected(champi::kMasterL, "Card:playback_FL"));
}

TEST(Router, SavedPeersMatchByNameThenAliasThenWithoutPipeWiresSuffix)
{
    FakeJack jack;
    jack.Add("Card:playback_FL", PortType::kAudio, false, true, {"alsa_pcm:hw:2:playback_1"});
    jack.Add("Card:playback_FR", PortType::kAudio, false, true);
    jack.Add("MiniFuse Loopback-63:capture_FL", PortType::kAudio, true, true);
    jack.Add("Synth-2:midi_in", PortType::kMidi, false);

    Graph g = jack.List();
    EXPECT_EQ(champi::Resolve(g, champi::kMasterL, "Card:playback_FR")->name, "Card:playback_FR");
    EXPECT_EQ(champi::Resolve(g, champi::kMasterL, "alsa_pcm:hw:2:playback_1")->name, "Card:playback_FL");
    EXPECT_EQ(champi::Resolve(g, champi::kLineL, "MiniFuse Loopback-62:capture_FL")->name,
              "MiniFuse Loopback-63:capture_FL");
    EXPECT_EQ(champi::Resolve(g, champi::kLineL, "MiniFuse Loopback:capture_FL")->name,
              "MiniFuse Loopback-63:capture_FL");
    EXPECT_EQ(champi::Resolve(g, champi::kMidiOut, "Synth:midi_in")->name, "Synth-2:midi_in");
    // The port must fit: an input can't connect to a playback port, nor audio to MIDI.
    EXPECT_EQ(champi::Resolve(g, champi::kLineL, "Card:playback_FL"), nullptr);
    EXPECT_EQ(champi::Resolve(g, champi::kMidiOut, "Card:playback_FL"), nullptr);
    EXPECT_EQ(champi::Resolve(g, champi::kMasterL, "Gone:playback_FL"), nullptr);
    // Only digits after the dash are a suffix.
    EXPECT_EQ(champi::Resolve(g, champi::kMidiOut, "Synth-x:midi_in"), nullptr);

    Router router(jack, Routing({{champi::kMasterL, {"alsa_pcm:hw:2:playback_1"}}}), true, nullptr);
    router.Update();
    EXPECT_TRUE(jack.Connected(champi::kMasterL, "Card:playback_FL"));
}

TEST(Router, NoConnectConnectsNothingButTheMenuStillWorks)
{
    FakeJack jack;
    jack.Typical();
    Saving saving;
    Router router(jack, Routing({{champi::kMasterL, {"Card:playback_FL"}}}), false, saving.Fn());
    router.Update();
    jack.Add("Midi-Bridge:Launchkey (capture)", PortType::kMidi, true, true);
    router.Update();
    EXPECT_TRUE(jack.connections.empty());

    router.Apply({{champi::kMidiOut, "Synth:midi_in", true}});
    EXPECT_TRUE(jack.Connected(champi::kMidiOut, "Synth:midi_in"));
    ASSERT_EQ(saving.saves, 1);
    EXPECT_EQ(saving.last->peers[champi::kMasterL], std::vector<std::string>{"Card:playback_FL"});
    EXPECT_EQ(saving.last->peers[champi::kMidiOut], std::vector<std::string>{"Synth:midi_in"});
}

TEST(Router, TheMenusChangesAreMadeAndSaved)
{
    FakeJack jack;
    jack.Typical();
    Saving saving;
    Router router(jack, std::nullopt, true, saving.Fn());
    router.Update();

    // A connection made elsewhere is shown but not saved.
    jack.Connect(champi::kMidiOut, "Synth:midi_in");
    router.Update();
    EXPECT_TRUE(router.graph().Connected(champi::kMidiOut, "Synth:midi_in"));

    router.Apply({{champi::kMasterL, "Card:playback_FL", false},
                  {champi::kMasterR, "Card:playback_FR", false},
                  {champi::kMasterL, "Other:playback_FL", true},
                  {champi::kMasterR, "Other:playback_FR", true}});
    EXPECT_TRUE(jack.Connected(champi::kMasterL, "Other:playback_FL"));
    EXPECT_FALSE(jack.Connected(champi::kMasterL, "Card:playback_FL"));
    EXPECT_TRUE(router.graph().Connected(champi::kMasterR, "Other:playback_FR"));
    ASSERT_EQ(saving.saves, 1);
    // The defaults that were connected are saved too.
    EXPECT_EQ(*saving.last, Routing({{champi::kMasterL, {"Other:playback_FL"}},
                                     {champi::kMasterR, {"Other:playback_FR"}},
                                     {champi::kEventsIn, {"Midi-Bridge:KeyStep (capture)"}}}));

    // From now on the saved routing rules: a new controller isn't connected on its own.
    jack.Add("Midi-Bridge:Launchkey (capture)", PortType::kMidi, true, true);
    router.Update();
    EXPECT_FALSE(jack.Connected(champi::kEventsIn, "Midi-Bridge:Launchkey (capture)"));
}

TEST(Router, UntickingAMissingPeerForgetsIt)
{
    FakeJack jack;
    jack.Typical();
    Saving saving;
    Router router(jack, Routing({{champi::kEventsIn, {"Gone:out", "Midi-Bridge:KeyStep (capture)"}}}), true,
                  saving.Fn());
    router.Update();
    router.Apply({{champi::kEventsIn, "Gone:out", false}});
    EXPECT_EQ(saving.last->peers[champi::kEventsIn], std::vector<std::string>{"Midi-Bridge:KeyStep (capture)"});
    EXPECT_TRUE(jack.Connected(champi::kEventsIn, "Midi-Bridge:KeyStep (capture)"));
}

TEST(Router, UntickingAPeerSavedUnderAnotherNameForgetsThatName)
{
    FakeJack jack;
    jack.Add("Synth-2:midi_in", PortType::kMidi, false);
    Saving saving;
    Router router(jack, Routing({{champi::kMidiOut, {"Synth:midi_in"}}}), true, saving.Fn());
    router.Update();
    EXPECT_TRUE(jack.Connected(champi::kMidiOut, "Synth-2:midi_in"));
    // Ticking it again doesn't save it twice.
    router.Apply({{champi::kMidiOut, "Synth-2:midi_in", true}});
    EXPECT_EQ(saving.last->peers[champi::kMidiOut], std::vector<std::string>{"Synth:midi_in"});
    router.Apply({{champi::kMidiOut, "Synth-2:midi_in", false}});
    EXPECT_TRUE(saving.last->peers[champi::kMidiOut].empty());
    EXPECT_TRUE(jack.connections.empty());
}

// ---- connections.toml ---------------------------------------------------------------------------

TEST(ConnectionsToml, RoundTrips)
{
    const SavedRouting r = Routing({{champi::kMasterL, {"MiniFuse 1 Main Output L/R:playback_FL"}},
                                    {champi::kEventsIn, {"Midi-Bridge:KeyStep 32 (capture)", "BLE MIDI 1:out"}},
                                    {champi::kMidiOut, {"odd \"name\\\":in"}}});
    const std::string toml = r.ToToml();
    EXPECT_NE(toml.find("master_l = [\"MiniFuse 1 Main Output L/R:playback_FL\"]\n"), std::string::npos);
    EXPECT_NE(toml.find("mic = []\n"), std::string::npos);
    EXPECT_EQ(SavedRouting::Parse(toml), r);
}

TEST(ConnectionsToml, AcceptsWhatPeopleWrite)
{
    const SavedRouting r = SavedRouting::Parse("# by hand\n"
                                               "master_l = 'Card:playback_FL'  # one, no list\n"
                                               "events-in = [\n"
                                               "  \"A:out\",\n"
                                               "  \"A:out\", # twice is once\n"
                                               "  \"B:out\",\n"
                                               "]\n");
    EXPECT_EQ(r, Routing({{champi::kMasterL, {"Card:playback_FL"}}, {champi::kEventsIn, {"A:out", "B:out"}}}));
}

TEST(ConnectionsToml, ErrorsNameTheLine)
{
    EXPECT_EQ(ParseError("mic = []\nmaster = [\"A:b\"]"), "connections.toml:2: CHAMPI has no port called \"master\"");
    EXPECT_EQ(ParseError("mic = []\n\nmic = []"), "connections.toml:3: mic is set twice");
    EXPECT_EQ(ParseError("midi-out = [\n\"A:b\",\n3]"), "connections.toml:3: port names go in quotes");
    EXPECT_EQ(ParseError("midi-out = [\"nocolon\"]"),
              "connections.toml:1: \"nocolon\" isn't a port: write \"client:port\"");
    EXPECT_EQ(ParseError("[ports]"),
              "connections.toml:1: tables aren't used in connections.toml; write `port = [\"client:port\"]` lines");
    EXPECT_EQ(ParseError("mic = A:b"), "connections.toml:1: expected a port name in quotes, or a [list] of them");
    EXPECT_EQ(ParseError("mic = [\"A:b\""), "connections.toml:1: expected ']'");
    EXPECT_EQ(ParseError("mic = \"A\\nb\""), "connections.toml:1: only \\\" and \\\\ can be escaped");
}

TEST(ConnectionsToml, SavesAndLoads)
{
    TempDir     dir;
    const auto  path = dir / "config/champi/connections.toml";
    EXPECT_FALSE(SavedRouting::Load(path));
    const SavedRouting r = Routing({{champi::kPhonesR, {"Card:playback_FR"}}});
    r.Save(path);
    EXPECT_EQ(SavedRouting::Load(path), r);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));

    WriteHostFile(path, "phones_r = [\"Card:playback_FR\"]\nfoo = []\n");
    try
    {
        SavedRouting::Load(path);
        FAIL();
    }
    catch(const std::runtime_error& e)
    {
        EXPECT_EQ(std::string(e.what()), path.string() + ":2: CHAMPI has no port called \"foo\"");
    }
}

// ---- The menu -----------------------------------------------------------------------------------

namespace
{
champi::RoutingSnapshot Snapshot(FakeJack& jack, SavedRouting saved = {})
{
    champi::RoutingSnapshot s;
    s.graph = jack.List();
    s.saved = std::move(saved);
    s.version++;
    return s;
}

std::vector<std::string> Labels(const std::vector<ConnectionsMenu::Row>& rows)
{
    std::vector<std::string> out;
    for(const auto& r : rows)
        out.push_back(r.label);
    return out;
}

std::vector<std::string> Lines(const ConnectionsMenu& menu)
{
    std::vector<std::string> out;
    for(const auto& item : menu.Items())
    {
        const char* tick = item.kind == Kind::kHeader   ? ""
                           : item.tick == Tick::kOn    ? "[x] "
                           : item.tick == Tick::kSome  ? "[-] "
                                                       : "[ ] ";
        out.push_back(tick + item.label + (item.missing ? " (missing)" : ""));
    }
    return out;
}

// The middle of a rectangle.
std::pair<float, float> Centre(const champi::layout::Rect& r)
{
    return {r.x + r.w / 2, r.y + r.h / 2};
}
} // namespace

TEST(ConnectionsMenu, ListsInputsAndOutputsWithPairsAsOneRow)
{
    FakeJack jack;
    jack.Typical();
    jack.Connect(champi::kMasterL, "Card:playback_FL");
    jack.Connect(champi::kMasterR, "Card:playback_FR");
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    EXPECT_EQ(Labels(menu.Column(0)), (std::vector<std::string>{"Mic", "Line in", "MIDI in"}));
    EXPECT_EQ(Labels(menu.Column(1)), (std::vector<std::string>{"Master", "Phones", "MIDI out"}));
    EXPECT_EQ(menu.Column(1)[0].connected, std::vector<std::string>{"Card: playback_FL/FR"});
    EXPECT_TRUE(menu.Column(1)[1].connected.empty());
}

TEST(ConnectionsMenu, APairRowListsPairsByClientAndAMonoPortGetsBothSides)
{
    FakeJack jack;
    jack.Typical();
    jack.Add("Mono:playback_MONO", PortType::kAudio, false, true);
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.SwitchColumn(); // Outputs: Master
    menu.Activate();
    ASSERT_TRUE(menu.InPeers());
    EXPECT_EQ(Lines(menu), (std::vector<std::string>{"[ ] Route left and right separately", "Card",
                                                     "[ ] playback_FL/FR", "Other", "[ ] playback_FL/FR", "Mono",
                                                     "[ ] playback_MONO (both sides)"}));
    // It starts on the first port, not the split line.
    EXPECT_EQ(menu.SelectedItem(), 2);

    std::vector<RouteChange> changes = menu.Activate();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].champi, champi::kMasterL);
    EXPECT_EQ(changes[0].peer, "Card:playback_FL");
    EXPECT_EQ(changes[1].champi, champi::kMasterR);
    EXPECT_EQ(changes[1].peer, "Card:playback_FR");
    EXPECT_TRUE(changes[0].connect);
    EXPECT_EQ(menu.Items()[2].tick, Tick::kOn); // shown at once

    menu.Move(0, 1); // Other's pair, past its header
    menu.Move(0, 1); // the mono port
    changes = menu.Activate();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].peer, "Mono:playback_MONO");
    EXPECT_EQ(changes[1].peer, "Mono:playback_MONO");
    EXPECT_EQ(changes[1].champi, champi::kMasterR);
}

TEST(ConnectionsMenu, AMonoRowListsEachPortThatFits)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.Move(0, 2); // MIDI in
    menu.Activate();
    EXPECT_EQ(Lines(menu), (std::vector<std::string>{"Midi-Bridge", "[ ] Midi Through: Port-0 (capture)",
                                                     "[ ] KeyStep (capture)", "Looper", "[ ] out"}));
    menu.Back();
    menu.Move(1, 0); // MIDI out, the same row on the other side
    EXPECT_EQ(menu.SelectedColumn(), 1);
    EXPECT_EQ(menu.SelectedRow(), 2);
    menu.Activate();
    EXPECT_EQ(Lines(menu), (std::vector<std::string>{"Midi-Bridge", "[ ] Midi Through: Port-0 (playback)",
                                                     "Synth", "[ ] midi_in"}));
}

TEST(ConnectionsMenu, OneSideConnectedSplitsThePair)
{
    FakeJack jack;
    jack.Typical();
    jack.Connect(champi::kMasterL, "Card:playback_FL");
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    EXPECT_EQ(Labels(menu.Column(1)), (std::vector<std::string>{"Master L", "Master R", "Phones", "MIDI out"}));
    menu.SwitchColumn();
    menu.Activate();
    EXPECT_EQ(Lines(menu), (std::vector<std::string>{"[x] Route left and right separately", "Card",
                                                     "[x] playback_FL", "[ ] playback_FR", "Other",
                                                     "[ ] playback_FL", "[ ] playback_FR"}));
}

TEST(ConnectionsMenu, ThePairCanBeSplitAndJoinedAgain)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.Move(0, 1); // Line in
    menu.Activate();
    menu.Move(0, -1);
    menu.Move(0, -1); // past the header, onto the split line
    EXPECT_EQ(menu.SelectedItem(), 0);
    EXPECT_TRUE(menu.Activate().empty());
    EXPECT_FALSE(menu.InPeers());
    EXPECT_EQ(Labels(menu.Column(0)), (std::vector<std::string>{"Mic", "Line in L", "Line in R", "MIDI in"}));
    EXPECT_EQ(menu.SelectedRow(), 1);

    menu.Move(0, 1); // Line in R
    menu.Activate();
    EXPECT_EQ(Lines(menu)[0], "[x] Route left and right separately");
    const auto changes = menu.Activate(); // its first port
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].champi, champi::kLineR);
    EXPECT_EQ(changes[0].peer, "Card:capture_FL");

    menu.Page(-1);
    EXPECT_EQ(menu.SelectedItem(), 0);
    menu.Activate();
    EXPECT_EQ(Labels(menu.Column(0)), (std::vector<std::string>{"Mic", "Line in", "MIDI in"}));
    EXPECT_EQ(menu.SelectedRow(), 1);
}

TEST(ConnectionsMenu, SavedPeersThatArentThereAreListedLast)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack, Routing({{champi::kMasterL, {"Gone:playback_1", "Card:playback_FL"}},
                                             {champi::kMasterR, {"Gone:playback_1"}}})));
    EXPECT_EQ(menu.Column(1)[0].missing, 1);
    menu.SwitchColumn();
    menu.Activate();
    const auto lines = Lines(menu);
    EXPECT_EQ(lines[lines.size() - 2], "Saved, not connected now");
    EXPECT_EQ(lines.back(), "[x] Gone:playback_1 (missing)");

    menu.Page(1);
    EXPECT_EQ(menu.SelectedItem(), int(lines.size()) - 1);
    const auto changes = menu.Activate();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_FALSE(changes[0].connect);
    EXPECT_EQ(changes[0].peer, "Gone:playback_1");
    EXPECT_EQ(changes[1].champi, champi::kMasterR);
}

TEST(ConnectionsMenu, EscapeGoesBackAndThenCloses)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.Activate();
    EXPECT_TRUE(menu.InPeers());
    EXPECT_TRUE(menu.Back());
    EXPECT_FALSE(menu.InPeers());
    EXPECT_FALSE(menu.Back());

    menu.Activate();
    menu.Reset(); // closed and opened again
    EXPECT_FALSE(menu.InPeers());
}

TEST(ConnectionsMenu, MovingStaysOnTheRows)
{
    FakeJack        jack;
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.Move(0, -1);
    EXPECT_EQ(menu.SelectedRow(), 0);
    menu.Move(0, 2);
    EXPECT_EQ(menu.SelectedRow(), 2);
    menu.Move(-1, 0);
    EXPECT_EQ(menu.SelectedColumn(), 0);
    menu.Move(5, 0);
    EXPECT_EQ(menu.SelectedColumn(), 1);
    EXPECT_EQ(menu.SelectedRow(), 2);
    EXPECT_FALSE(menu.MappingSelected());
    menu.Move(0, -9);
    EXPECT_EQ(menu.SelectedRow(), 0);
    // Nothing fits Master: only the split line.
    menu.Activate();
    EXPECT_EQ(Lines(menu), std::vector<std::string>{"[ ] Route left and right separately"});
    EXPECT_EQ(menu.SelectedItem(), 0);
}

TEST(ConnectionsMenu, TheMappingRowIsUnderBothColumns)
{
    FakeJack        jack;
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));

    // Down off the last row of either column reaches it; up goes back to that column.
    menu.Move(0, 9);
    EXPECT_TRUE(menu.MappingSelected());
    menu.Move(1, 0); // nothing to the side
    menu.SwitchColumn();
    EXPECT_TRUE(menu.MappingSelected());
    menu.Move(0, -1);
    EXPECT_FALSE(menu.MappingSelected());
    EXPECT_EQ(menu.SelectedColumn(), 0);
    EXPECT_EQ(menu.SelectedRow(), 2);
    menu.SwitchColumn();
    menu.Move(0, 1);
    EXPECT_TRUE(menu.MappingSelected());
    menu.Move(0, -2);
    EXPECT_EQ(menu.SelectedColumn(), 1);
    EXPECT_EQ(menu.SelectedRow(), 1);

    // Enter opens it, and Esc closes it again; a click opens it too.
    menu.Page(1);
    menu.Move(0, 1);
    EXPECT_TRUE(menu.Activate().empty());
    EXPECT_TRUE(menu.InMapping());
    EXPECT_FALSE(menu.InPeers());
    EXPECT_TRUE(menu.Back());
    EXPECT_FALSE(menu.InMapping());
    EXPECT_TRUE(menu.MappingSelected());
    EXPECT_FALSE(menu.Back());

    const auto [x, y] = Centre(ConnectionsMenu::kMappingRow);
    menu.Click(x, y);
    EXPECT_TRUE(menu.InMapping());
    const auto [bx, by] = Centre(ConnectionsMenu::kBack);
    menu.Click(bx, by);
    EXPECT_FALSE(menu.InMapping());
    menu.Activate();
    menu.Reset(); // closed and opened again
    EXPECT_FALSE(menu.InMapping());
}

TEST(ConnectionsMenu, TheSelectionFollowsItsPortWhenTheGraphChanges)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.Move(0, 2); // MIDI in
    menu.Activate();
    menu.Move(0, 1); // KeyStep
    EXPECT_EQ(Lines(menu)[menu.SelectedItem()], "[ ] KeyStep (capture)");

    jack.ports.insert(jack.ports.begin(), PeerPort{"Aaa:out", "", {}, PortType::kMidi, true, true});
    jack.Connect(champi::kEventsIn, "Midi-Bridge:KeyStep (capture)");
    menu.SetSnapshot(Snapshot(jack));
    EXPECT_EQ(Lines(menu)[menu.SelectedItem()], "[x] KeyStep (capture)");
}

TEST(ConnectionsMenu, ClicksOpenRowsTickPortsAndGoBack)
{
    FakeJack jack;
    jack.Typical();
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));

    // Outside every row: nothing.
    EXPECT_TRUE(menu.Click(1, 1).empty());
    EXPECT_FALSE(menu.InPeers());

    auto [rx, ry] = Centre(ConnectionsMenu::RowRect(1, 2)); // MIDI out
    menu.Click(rx, ry);
    ASSERT_TRUE(menu.InPeers());
    EXPECT_EQ(menu.OpenRow().label, "MIDI out");

    // The header is not a target; the port under it is.
    auto [hx, hy] = Centre(menu.ItemRect(2));
    EXPECT_TRUE(menu.Click(hx, hy).empty());
    auto [ix, iy] = Centre(menu.ItemRect(3));
    const auto changes = menu.Click(ix, iy);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].peer, "Synth:midi_in");
    EXPECT_EQ(menu.SelectedItem(), 3);

    auto [bx, by] = Centre(ConnectionsMenu::kBack);
    menu.Click(bx, by);
    EXPECT_FALSE(menu.InPeers());
}

TEST(ConnectionsMenu, ALongListScrolls)
{
    FakeJack jack;
    for(int i = 0; i < 40; i++)
        jack.Add("Many:in_" + std::to_string(i), PortType::kMidi, false);
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    menu.SwitchColumn();
    menu.Move(0, 2);
    menu.Activate();
    const int lines = ConnectionsMenu::VisibleLines();
    ASSERT_GT(lines, 5);
    EXPECT_EQ(menu.FirstVisible(), 0);

    for(int i = 0; i < lines + 3; i++)
        menu.Move(0, 1);
    EXPECT_EQ(menu.SelectedItem(), lines + 4);
    EXPECT_EQ(menu.FirstVisible(), menu.SelectedItem() - lines + 1);

    // A click lands on the line drawn there, wherever the list is scrolled to.
    auto [x, y] = Centre(menu.ItemRect(menu.FirstVisible() + 1));
    const auto changes = menu.Click(x, y);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].peer, "Many:in_" + std::to_string(menu.FirstVisible()));

    menu.ScrollBy(-100);
    EXPECT_EQ(menu.FirstVisible(), 0);
    menu.ScrollBy(100);
    EXPECT_EQ(menu.FirstVisible(), int(menu.Items().size()) - lines);
    menu.Page(1);
    menu.Page(1);
    menu.Page(1);
    menu.Page(1);
    EXPECT_EQ(menu.SelectedItem(), int(menu.Items().size()) - 1);
}

TEST(ConnectionsMenu, LeftAndRightSetTheSelectedInputsVolume)
{
    FakeJack        jack;
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    const auto& levels = menu.Levels().percent;

    // The mic: left and right step its volume, and stop at the ends.
    menu.Move(-1, 0);
    EXPECT_EQ(levels[champi::kMic], 95);
    EXPECT_EQ(menu.SelectedColumn(), 0);
    menu.Move(1, 0);
    menu.Move(1, 0);
    EXPECT_EQ(levels[champi::kMic], 100);
    EXPECT_EQ(menu.SelectedColumn(), 0);

    // Line in moves both sides; split, each side has its own.
    menu.Move(0, 1);
    menu.Move(-1, 0);
    EXPECT_EQ(levels[champi::kLineL], 95);
    EXPECT_EQ(levels[champi::kLineR], 95);
    menu.Activate(); // the split line
    menu.Activate();
    ASSERT_EQ(Labels(menu.Column(0)), (std::vector<std::string>{"Mic", "Line in L", "Line in R", "MIDI in"}));
    menu.Move(0, 1);
    menu.Move(-1, 0);
    EXPECT_EQ(levels[champi::kLineL], 95);
    EXPECT_EQ(levels[champi::kLineR], 90);

    // MIDI in has no volume: right goes to the outputs, and left comes back. Tab always switches.
    menu.Move(0, 1);
    EXPECT_FALSE(ConnectionsMenu::HasLevel(menu.OpenRow()));
    menu.Move(1, 0);
    EXPECT_EQ(menu.SelectedColumn(), 1);
    menu.Move(-1, 0);
    EXPECT_EQ(menu.SelectedColumn(), 0);
    menu.Move(0, -9);
    menu.SwitchColumn();
    EXPECT_EQ(menu.SelectedColumn(), 1);
    menu.SwitchColumn();
    EXPECT_EQ(menu.SelectedColumn(), 0);
    EXPECT_EQ(levels[champi::kMic], 100);
}

TEST(ConnectionsMenu, LeftAndRightSetTheSelectedOutputsVolume)
{
    FakeJack        jack;
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    const auto& levels = menu.Levels().percent;

    // Master moves both sides; split phones, each side has its own.
    menu.SwitchColumn();
    menu.Move(-1, 0);
    EXPECT_EQ(menu.SelectedColumn(), 1);
    EXPECT_EQ(levels[champi::kMasterL], 45);
    EXPECT_EQ(levels[champi::kMasterR], 45);
    menu.Move(0, 1);
    menu.Activate(); // the split line
    menu.Activate();
    ASSERT_EQ(Labels(menu.Column(1)), (std::vector<std::string>{"Master", "Phones L", "Phones R", "MIDI out"}));
    menu.Move(0, 1); // the split leaves Phones L selected
    menu.Move(-1, 0);
    menu.Move(-1, 0);
    EXPECT_EQ(levels[champi::kPhonesL], 50);
    EXPECT_EQ(levels[champi::kPhonesR], 40);

    // MIDI out has no volume: left goes back to the inputs.
    menu.Move(0, 1);
    EXPECT_FALSE(ConnectionsMenu::HasLevel(menu.OpenRow()));
    menu.Move(-1, 0);
    EXPECT_EQ(menu.SelectedColumn(), 0);
}

TEST(ConnectionsMenu, AClickOnTheVolumeBarSetsIt)
{
    FakeJack        jack;
    ConnectionsMenu menu;
    menu.SetSnapshot(Snapshot(jack));
    const champi::layout::Rect bar = ConnectionsMenu::LevelRect(0, 0);
    EXPECT_TRUE(menu.Click(bar.x + bar.w * 0.3f, bar.y + bar.h / 2).empty());
    EXPECT_FALSE(menu.InPeers());
    EXPECT_EQ(menu.Levels().percent[champi::kMic], 30);
    menu.Click(bar.x - 0.5f, bar.y);
    EXPECT_EQ(menu.Levels().percent[champi::kMic], 0);

    // The outputs' bars too, once their row is selected.
    menu.SwitchColumn();
    const champi::layout::Rect master = ConnectionsMenu::LevelRect(1, 0);
    menu.Click(master.x + master.w * 0.6f, master.y + master.h / 2);
    EXPECT_FALSE(menu.InPeers());
    EXPECT_EQ(menu.Levels().percent[champi::kMasterL], 60);
    EXPECT_EQ(menu.Levels().percent[champi::kMasterR], 60);
    menu.SwitchColumn();

    // Elsewhere on the row opens it.
    auto [rx, ry] = Centre(ConnectionsMenu::RowRect(0, 0));
    menu.Click(rx - 30, ry);
    EXPECT_TRUE(menu.InPeers());
}

// ---- audio_levels.toml ----------------------------------------------------------------------------

TEST(AudioLevelsToml, RoundTripsAndDefaultsToUnity)
{
    using champi::AudioLevels;
    AudioLevels levels;
    levels.percent[champi::kMic]     = 60;
    levels.percent[champi::kLineR]   = 35;
    levels.percent[champi::kPhonesL] = 80;
    EXPECT_EQ(AudioLevels::Parse(levels.ToToml()), levels);
    EXPECT_EQ(AudioLevels::Parse("line_r = 35\nmic = 60 # quieter\nphones_l = 80\n"), levels);
    EXPECT_EQ(AudioLevels::Parse(""), AudioLevels{});
    EXPECT_EQ(AudioLevels{}.ToToml().find("events-in"), std::string::npos) << "MIDI has no volume";
    EXPECT_EQ(AudioLevels{}.percent[champi::kLineL], 100);
    EXPECT_EQ(AudioLevels{}.percent[champi::kMasterL], 50);
    EXPECT_EQ(AudioLevels{}.percent[champi::kPhonesR], 50);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kMic, 100), 1.f);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kMic, 50), 0.125f);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kMic, 0), 0.f);
    // Outputs pass as they are at 50, and go up to +18 dB.
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kMasterL, 50), 1.f);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kPhonesR, 100), 8.f);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kMasterR, 25), 0.125f);
    EXPECT_FLOAT_EQ(AudioLevels::Gain(champi::kPhonesL, 0), 0.f);
}

TEST(AudioLevelsToml, ErrorsNameTheLine)
{
    using champi::AudioLevels;
    auto error = [](const char* toml) {
        try
        {
            AudioLevels::Parse(toml, "audio_levels.toml");
        }
        catch(const std::runtime_error& e)
        {
            return std::string(e.what());
        }
        return std::string();
    };
    EXPECT_EQ(error("mic = 50\nmaster = 3"), "audio_levels.toml:2: CHAMPI has no audio port called \"master\"");
    EXPECT_EQ(error("midi-out = 3"), "audio_levels.toml:1: CHAMPI has no audio port called \"midi-out\"");
    EXPECT_EQ(error("mic = 50\nmic = 3"), "audio_levels.toml:2: mic is set twice");
    EXPECT_EQ(error("master_l = 101"), "audio_levels.toml:1: a volume is a number from 0 to 100");
    EXPECT_EQ(error("mic = \"50\""), "audio_levels.toml:1: a volume is a number from 0 to 100");
}

TEST(AudioLevelsToml, SavesAndLoads)
{
    using champi::AudioLevels;
    TempDir    dir;
    const auto path = dir / "config/champi/audio_levels.toml";
    EXPECT_FALSE(AudioLevels::Load(path));
    AudioLevels levels;
    levels.percent[champi::kMic]     = 45;
    levels.percent[champi::kMasterR] = 70;
    levels.Save(path);
    EXPECT_EQ(AudioLevels::Load(path), levels);
}
