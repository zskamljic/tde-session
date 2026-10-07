#include "Locker.hpp"

#include <clocale>
#include <cstdio>
#include <cstring>

int main(int argc, char* argv[])
{
    bool seconds = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seconds") == 0) {
            seconds = true;
            continue;
        }
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts("tde-cerberus " TDE_SESSION_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::puts("Usage: tde-cerberus [--seconds]\n\n"
                      "Cerberus, the lock screen of TDE. Locks the session until the password is given;\n"
                      "prints \"locked\" once the session is locked. With --seconds, the clock shows them.");
            return 0;
        }
    }
    // Dates and times in the user's language.
    std::setlocale(LC_ALL, "");
    return cerberus::lockSession(seconds);
}
