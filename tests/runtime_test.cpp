// The Runtime with TAPE's firmware library: starting, power-cycling to another card, and a card
// that no longer passes its check when it goes in. One Runtime per process, so one test.
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <sched.h>
#include <sstream>
#include <sys/resource.h>
#include <thread>

#include "daisy_seed.h"
#include "daisycola/host.h"
#include "runtime.h"
#include "test_util.h"

namespace fs = std::filesystem;
using champi::Runtime;

namespace
{
void WaitBooted(Runtime& runtime, const char* what)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while(!runtime.Booted())
    {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline) << what << ": boot timed out";
        ASSERT_TRUE(daisycola::GetBoardState().running) << what;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Threads of this process under SCHED_FIFO, from the policy field (41) of /proc's stat.
int FifoThreads()
{
    int count = 0;
    for(const fs::directory_entry& task : fs::directory_iterator("/proc/self/task"))
    {
        std::ifstream     file(task.path() / "stat");
        const std::string stat((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const size_t      close = stat.rfind(')');
        if(close == std::string::npos)
            continue;
        std::istringstream fields(stat.substr(close + 2)); // from field 3, the state
        std::string        field;
        for(int i = 3; i <= 41 && fields >> field; i++)
            if(i == 41 && std::stoi(field) == SCHED_FIFO)
                count++;
    }
    return count;
}

bool RealtimeAllowed()
{
    rlimit limit{};
    return getrlimit(RLIMIT_RTPRIO, &limit) == 0 && limit.rlim_cur >= 1;
}

fs::path CopyFactory(const TempDir& dir, const std::string& name)
{
    fs::copy(CHAMPI_FACTORY_CARD_DIR, dir / name);
    return dir / name;
}
} // namespace

TEST(Runtime, PowerCyclesToAnotherCard)
{
    TempDir        dir;
    const fs::path a = CopyFactory(dir, "a");
    const fs::path b = CopyFactory(dir, "b");
    std::string    options = ReadHostFile(b / "options.json");
    const size_t   at      = options.find("1", options.find("Midi In Channel"));
    options.replace(at, 1, "5");
    WriteHostFile(b / "options.json", options);
    const fs::path c = CopyFactory(dir, "c");

    Runtime& runtime = Runtime::Get();
    runtime.Start(a);
    WaitBooted(runtime, "first start");
    EXPECT_EQ(runtime.Card(), a);
    EXPECT_EQ(runtime.MidiInChannel(), 0);
    EXPECT_THROW(runtime.Start(a), std::logic_error);

    // TAPE's thread at real-time priority, where this machine allows it, also after a power cycle.
    const bool realtime = RealtimeAllowed();
    if(realtime)
    {
        EXPECT_EQ(FifoThreads(), 0);
        EXPECT_TRUE(runtime.SetFirmwarePriority(1));
        EXPECT_EQ(FifoThreads(), 1);
    }

    // The toggle and the line-in plug stay where the player left them.
    runtime.Panel().SetToggle(false);
    runtime.Panel().SetLineIn(true);

    EXPECT_EQ(runtime.PowerCycle(b, a), std::vector<champi::CardProblem>{});
    EXPECT_FALSE(runtime.Booted()) << "TAPE starts over";
    WaitBooted(runtime, "after the first cycle");
    EXPECT_EQ(runtime.Card(), b);
    EXPECT_EQ(runtime.MidiInChannel(), 4);
    EXPECT_FALSE(runtime.Panel().Toggle());
    EXPECT_TRUE(daisycola::GetPin(daisy::seed::D21)) << "the line-in plug, wired again";
    EXPECT_TRUE(daisycola::GetAudioFormat().started);
    if(realtime)
    {
        EXPECT_EQ(FifoThreads(), 1) << "the new firmware thread gets the priority too";
    }

    // A card that stopped passing between the window's check and the insert: the old one goes back.
    WriteHostFile(c / "notes.txt", "x");
    const std::vector<champi::CardProblem> problems = runtime.PowerCycle(c, b);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].path, fs::path("notes.txt"));
    WaitBooted(runtime, "after falling back");
    EXPECT_EQ(runtime.Card(), b);

    // Without a fallback, nothing goes in and TAPE stays off until a card does.
    EXPECT_EQ(runtime.PowerCycle(c, {}).size(), 1u);
    EXPECT_TRUE(runtime.Card().empty());
    EXPECT_FALSE(runtime.Running());
    fs::remove(c / "notes.txt");
    EXPECT_EQ(runtime.PowerCycle(c, {}), std::vector<champi::CardProblem>{});
    WaitBooted(runtime, "a card into an empty slot");
    EXPECT_EQ(runtime.Card(), c);

    if(realtime)
    {
        EXPECT_TRUE(runtime.SetFirmwarePriority(0));
        EXPECT_EQ(FifoThreads(), 0);
    }
    EXPECT_TRUE(runtime.Stop());
}
