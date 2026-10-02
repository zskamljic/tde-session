#include "Password.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using cerberus::Password;

namespace {

int failures = 0;

void expect(bool condition, const char* what)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

} // namespace

int main()
{
    Password password;
    expect(password.empty() && password.length() == 0, "starts empty");

    password.append("ab");
    password.append("č"); // two bytes
    password.append("€"); // three bytes
    expect(std::strcmp(password.data(), "abč€") == 0, "keeps what was typed");
    expect(password.length() == 4, "counts characters, not bytes");

    password.removeLast();
    expect(std::strcmp(password.data(), "abč") == 0, "removes a whole character");
    password.removeLast();
    password.removeLast();
    password.removeLast();
    password.removeLast();
    expect(password.empty(), "removing past the start leaves it empty");

    expect(!password.append(std::string(2000, 'x')), "refuses more than it holds");
    expect(password.empty(), "and keeps nothing of it");

    password.append("secret");
    password.clear();
    expect(password.empty() && password.data()[0] == '\0', "clearing empties it");

    if (failures == 0)
        std::puts("all password tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
