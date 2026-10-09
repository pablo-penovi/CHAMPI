// A second, small JACK client next to CHAMPI's own. DPF's standalone doesn't connect any ports and
// doesn't report xruns, so this one:
//   - keeps CHAMPI's connections in step with the saved routing (see Router): restored once
//     CHAMPI's ports exist and whenever a saved peer appears, the defaults without a saved file;
//   - serves the connections menu: a snapshot of the graph, and the changes it asks for;
//   - counts the xruns JACK reports, into g_xruns.
//
// JACK functions can't be called from JACK's callbacks, and the UI thread shouldn't call them at
// all (under PipeWire they can block), so all of it runs on the monitor's own thread. The
// callbacks only wake it.
#pragma once

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "routing.h"

typedef struct _jack_client jack_client_t;

namespace champi
{
class JackMonitor : public RoutingService
{
  public:
    ~JackMonitor() override { Close(); }

    /**
     * Opens the client and starts the thread. `saved` is the routing read from `file`, empty if
     * there was none; changes from the menu are written to `file`. Without `auto_connect` nothing
     * is connected on its own. Returns false if there is no JACK server, which isn't an error: DPF
     * then falls back to native audio.
     */
    bool Open(const std::string& client, bool auto_connect, std::filesystem::path file,
              std::optional<SavedRouting> saved);
    void Close();

    std::optional<RoutingSnapshot> SnapshotIfNewer(uint64_t have) override;
    void                           Request(std::vector<RouteChange> changes) override;

  private:
    void Run(bool auto_connect, std::optional<SavedRouting> saved);
    void Wake();

    friend struct JackCallbacks;

    jack_client_t*        jack_ = nullptr;
    std::string           client_;
    std::filesystem::path file_;
    std::thread           thread_;

    std::mutex                            mutex_;
    std::condition_variable               wake_;
    bool                                  changed_ = true; // the graph changed: list it again
    bool                                  quit_    = false;
    std::vector<std::vector<RouteChange>> requests_;
    RoutingSnapshot                       snapshot_; // version 0 until the first listing
};

} // namespace champi
