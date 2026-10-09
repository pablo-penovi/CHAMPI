// A second, small JACK client next to CHAMPI's own. DPF's standalone doesn't connect any ports and
// doesn't report xruns, so this one does both:
//   - once CHAMPI's ports appear, it connects master out to the first two system playback ports
//     and every hardware MIDI source to CHAMPI's MIDI input (PipeWire lists ALSA sequencer
//     devices there, so this also covers the "ALSA-seq auto-connect" of the plan);
//   - it counts the xruns JACK reports, into g_xruns.
#pragma once

#include <string>
#include <thread>

typedef struct _jack_client jack_client_t;

namespace champi
{
class JackMonitor
{
  public:
    ~JackMonitor() { Close(); }

    /** Opens the client. With `connect`, connects `client`'s ports in the background once they
     *  exist. Returns false if there is no JACK server, which isn't an error: DPF then falls back
     *  to native audio. */
    bool Open(const std::string& client, bool connect);
    void Close();

  private:
    void Connect(std::string client);

    jack_client_t* jack_ = nullptr;
    std::thread    connector_;
};

} // namespace champi
