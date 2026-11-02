#include "WindowMemory.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

#include <unistd.h>

using atlas::RememberedWindow;
using atlas::WindowMemory;

namespace {

int failures = 0;

void expect(const char* name, bool ok)
{
    if (ok)
        return;
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", name);
}

} // namespace

int main()
{
    const RememberedWindow brave {"brave-browser", "Dell U2720Q ABC123", 100, -20, 1200, 800, 0};
    const RememberedWindow slack {"Slack", "BOE 0x0BCA (eDP-1)", 0, 0, 960, 1000, 2};

    expect("written lines read back",
        WindowMemory::parse(WindowMemory::format({brave, slack})) == std::vector {brave, slack});
    expect("broken lines are passed by",
        WindowMemory::parse("x\tD\t1\t2\t3\n\tD\t1\t2\t3\t4\t0\nx\tD\t1\t2\t0\t4\t0\nx\tD\t1\t2\t3\t4\t9\n").empty());
    expect("the last line of an application counts",
        WindowMemory::parse("a\tD\t1\t1\t5\t5\t0\na\tD\t2\t2\t6\t6\t1\n")
            == std::vector {RememberedWindow {"a", "D", 2, 2, 6, 6, 1}});

    const std::string dir = std::filesystem::temp_directory_path() / ("tst_windowmemory." + std::to_string(::getpid()));
    const std::string path = dir + "/tde/windows";
    {
        WindowMemory memory(path);
        expect("nothing at first", memory.find("brave-browser") == nullptr);
        memory.remember(brave);
        memory.remember(slack);
        RememberedWindow moved = brave;
        moved.x = 400;
        memory.remember(moved);
        expect("found", memory.find("brave-browser") && memory.find("brave-browser")->x == 400);
        memory.remember({"with\ttab", "D", 0, 0, 10, 10, 0});
        expect("ids that cannot be written are not kept", memory.find("with\ttab") == nullptr);
    }
    WindowMemory again(path);
    expect("kept across starts", again.find("Slack") && *again.find("Slack") == slack);
    expect("moved one kept", again.find("brave-browser") && again.find("brave-browser")->x == 400);
    std::filesystem::remove_all(dir);
    return failures == 0 ? 0 : 1;
}
