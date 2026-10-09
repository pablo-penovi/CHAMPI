#include "jack_monitor.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <jack/jack.h>

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
} // namespace

bool JackMonitor::Open(const std::string& client, bool connect)
{
    jack_status_t status;
    jack_ = jack_client_open((client + " monitor").c_str(), JackNoStartServer, &status);
    if(!jack_)
        return false;
    jack_set_xrun_callback(jack_, OnXrun, nullptr);
    if(jack_activate(jack_) != 0)
    {
        jack_client_close(jack_);
        jack_ = nullptr;
        return false;
    }
    if(connect)
        connector_ = std::thread(&JackMonitor::Connect, this, client);
    return true;
}

void JackMonitor::Close()
{
    if(connector_.joinable())
        connector_.join();
    if(jack_)
        jack_client_close(jack_);
    jack_ = nullptr;
}

void JackMonitor::Connect(std::string client)
{
    // The firmware takes a moment to start, and DPF registers its ports after that.
    const std::string master_l = client + ":master_l";
    const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(!jack_port_by_name(jack_, master_l.c_str()))
    {
        if(std::chrono::steady_clock::now() > deadline)
        {
            std::fprintf(stderr, "champi: %s didn't appear, not connecting\n", master_l.c_str());
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const PortList playback{jack_get_ports(jack_, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                           JackPortIsPhysical | JackPortIsInput)};
    const char* const outs[] = {"master_l", "master_r"};
    for(int i = 0; i < 2 && playback.names && playback.names[i]; i++)
        jack_connect(jack_, (client + ":" + outs[i]).c_str(), playback.names[i]);

    // Every hardware MIDI source, except the kernel's loopback port.
    const std::string midi_in = client + ":events-in";
    const PortList    sources{jack_get_ports(jack_, nullptr, JACK_DEFAULT_MIDI_TYPE,
                                             JackPortIsPhysical | JackPortIsOutput)};
    for(int i = 0; sources.names && sources.names[i]; i++)
        if(!std::strstr(sources.names[i], "Midi Through"))
            jack_connect(jack_, sources.names[i], midi_in.c_str());
}

} // namespace champi
