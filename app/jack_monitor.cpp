#include "jack_monitor.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>

#include <jack/jack.h>
#include <jack/metadata.h>

#include "app.h"

namespace champi
{
namespace
{
int OnXrun(void*)
{
    g_xruns.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

// jack_get_ports' list, freed when it goes out of scope.
struct PortList
{
    const char** names;
    ~PortList()
    {
        if(names)
            jack_free(names);
    }
};

// The graph as RoutingBackend sees it, through the monitor's client.
class JackBackend : public RoutingBackend
{
  public:
    JackBackend(jack_client_t* jack, const std::string& client) : jack_(jack), prefix_(client + ":") {}

    Graph List() override
    {
        Graph g;
        g.champi_present = true;
        for(const ChampiPort& c : kChampiPorts)
            if(!jack_port_by_name(jack_, (prefix_ + c.name).c_str()))
                g.champi_present = false;

        for(PortType type : {PortType::kAudio, PortType::kMidi})
            for(bool output : {true, false})
            {
                const char*         jack_type = type == PortType::kAudio ? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE;
                const unsigned long flags     = output ? JackPortIsOutput : JackPortIsInput;
                const PortList      all{jack_get_ports(jack_, nullptr, jack_type, flags)};
                const PortList      physical{jack_get_ports(jack_, nullptr, jack_type, flags | JackPortIsPhysical)};
                for(int i = 0; all.names && all.names[i]; i++)
                {
                    if(std::strncmp(all.names[i], prefix_.c_str(), prefix_.size()) == 0)
                        continue;
                    PeerPort p;
                    p.name   = all.names[i];
                    p.type   = type;
                    p.output = output;
                    for(int k = 0; physical.names && physical.names[k]; k++)
                        p.physical |= p.name == physical.names[k];
                    if(jack_port_t* port = jack_port_by_name(jack_, all.names[i]))
                        Describe(port, p);
                    g.ports.push_back(std::move(p));
                }
            }

        if(g.champi_present)
            for(int c = 0; c < kNumChampiPorts; c++)
            {
                jack_port_t*   port = jack_port_by_name(jack_, (prefix_ + kChampiPorts[c].name).c_str());
                const PortList peers{port ? jack_port_get_all_connections(jack_, port) : nullptr};
                for(int i = 0; peers.names && peers.names[i]; i++)
                    if(std::strncmp(peers.names[i], prefix_.c_str(), prefix_.size()) != 0)
                        g.connections.insert({c, peers.names[i]});
            }
        return g;
    }

    bool Connect(int champi, const std::string& peer) override
    {
        const int r = Ports(champi, peer, jack_connect);
        return r == 0 || r == EEXIST;
    }

    bool Disconnect(int champi, const std::string& peer) override
    {
        return Ports(champi, peer, jack_disconnect) == 0;
    }

  private:
    // Calls `f` with the source first, as jack_connect and jack_disconnect want.
    int Ports(int champi, const std::string& peer, int (*f)(jack_client_t*, const char*, const char*))
    {
        const std::string own = prefix_ + kChampiPorts[champi].name;
        return kChampiPorts[champi].input ? f(jack_, peer.c_str(), own.c_str()) : f(jack_, own.c_str(), peer.c_str());
    }

    // The port's aliases and pretty-name.
    void Describe(jack_port_t* port, PeerPort& p)
    {
        const int size = jack_port_name_size();
        std::string a(size, '\0'), b(size, '\0');
        char*       aliases[2] = {a.data(), b.data()};
        const int   n          = jack_port_get_aliases(port, aliases);
        for(int i = 0; i < n && i < 2; i++)
            if(*aliases[i])
                p.aliases.push_back(aliases[i]);

        char* value = nullptr;
        char* type  = nullptr;
        if(jack_get_property(jack_port_uuid(port), JACK_METADATA_PRETTY_NAME, &value, &type) == 0)
        {
            if(value)
                p.pretty = value;
            jack_free(value);
            jack_free(type);
        }
    }

    jack_client_t*    jack_;
    const std::string prefix_;
};
} // namespace

// JACK's callbacks, which run on its notification thread: they only wake the monitor.
struct JackCallbacks
{
    static void PortRegistered(jack_port_id_t, int, void* arg) { static_cast<JackMonitor*>(arg)->Wake(); }
    static void PortConnected(jack_port_id_t, jack_port_id_t, int, void* arg)
    {
        static_cast<JackMonitor*>(arg)->Wake();
    }
    static void PortRenamed(jack_port_id_t, const char*, const char*, void* arg)
    {
        static_cast<JackMonitor*>(arg)->Wake();
    }
};

bool JackMonitor::Open(const std::string& client, bool auto_connect, std::filesystem::path file,
                       std::optional<SavedRouting> saved)
{
    jack_status_t status;
    jack_ = jack_client_open((client + " monitor").c_str(), JackNoStartServer, &status);
    if(!jack_)
        return false;
    client_ = client;
    file_   = std::move(file);
    jack_set_xrun_callback(jack_, OnXrun, nullptr);
    jack_set_port_registration_callback(jack_, JackCallbacks::PortRegistered, this);
    jack_set_port_connect_callback(jack_, JackCallbacks::PortConnected, this);
    jack_set_port_rename_callback(jack_, JackCallbacks::PortRenamed, this);
    if(jack_activate(jack_) != 0)
    {
        jack_client_close(jack_);
        jack_ = nullptr;
        return false;
    }
    thread_ = std::thread(&JackMonitor::Run, this, auto_connect, std::move(saved));
    return true;
}

void JackMonitor::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_one();
    if(thread_.joinable())
        thread_.join();
    if(jack_)
        jack_client_close(jack_);
    jack_ = nullptr;
}

void JackMonitor::Wake()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed_ = true;
    }
    wake_.notify_one();
}

std::optional<RoutingSnapshot> JackMonitor::SnapshotIfNewer(uint64_t have)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(snapshot_.version == have)
        return std::nullopt;
    return snapshot_;
}

void JackMonitor::Request(std::vector<RouteChange> changes)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(std::move(changes));
    }
    wake_.notify_one();
}

void JackMonitor::Run(bool auto_connect, std::optional<SavedRouting> saved)
{
    JackBackend backend(jack_, client_);
    Router      router(backend, std::move(saved), auto_connect, [this](const SavedRouting& routing) {
        try
        {
            routing.Save(file_);
        }
        catch(const std::exception& e)
        {
            std::fprintf(stderr, "champi: can't save the connections: %s\n", e.what());
        }
    });

    std::unique_lock<std::mutex> lock(mutex_);
    for(;;)
    {
        wake_.wait(lock, [&] { return quit_ || changed_ || !requests_.empty(); });
        if(quit_)
            return;
        // A device coming and going registers its ports in a burst: take them in one go.
        if(changed_ && requests_.empty())
        {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            lock.lock();
        }
        auto requests = std::move(requests_);
        requests_.clear();
        changed_ = false;
        lock.unlock();

        for(const auto& changes : requests)
            router.Apply(changes);
        router.Update();

        lock.lock();
        snapshot_.graph = router.graph();
        snapshot_.saved = router.saved();
        snapshot_.version++;
    }
}

} // namespace champi
