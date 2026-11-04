#include "Greetd.hpp"
#include "Json.hpp"
#include "Sessions.hpp"
#include "Users.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace charon;

namespace {

int failures = 0;

void expect(bool condition, const char* what)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

void testJson()
{
    expect(json::quoted("a\"b\\c\nd") == R"("a\"b\\c\nd")", "quotes and escapes");
    expect(json::quoted(std::string("\x01", 1)) == R"("\u0001")", "escapes control characters");
    expect(json::list({"sh", "-c", "exec x"}) == R"(["sh","-c","exec x"])", "makes a list");
    expect(json::list({}) == "[]", "makes an empty list");

    const auto object = json::parseObject(R"( {"type": "auth_message", "auth_message_type":"secret",
        "auth_message": "Password: ", "n": 3, "b": true, "x": null, "o": {"a": [1, "2"]}} )");
    expect(object && object->at("type") == "auth_message", "reads strings");
    expect(object && object->at("auth_message") == "Password: ", "keeps spaces");
    expect(object && !object->contains("n") && !object->contains("o"), "passes other values by");

    const auto escaped = json::parseObject(R"({"a":"č\n\"😀"})");
    expect(escaped && escaped->at("a") == "č\n\"😀", "reads escapes and surrogate pairs");
    expect(!json::parseObject(R"({"a":"b")"), "refuses what is cut short");
    expect(!json::parseObject(R"({"a":"b"} x)"), "refuses what follows");
    expect(json::parseObject("{}") && json::parseObject("{}")->empty(), "reads an empty object");
}

void testReplies()
{
    auto reply = parseReply(R"({"type":"success"})");
    expect(reply && reply->kind == Reply::Kind::Success, "success");
    reply = parseReply(R"({"type":"error","error_type":"auth_error","description":"pam_authenticate: AUTH_ERR"})");
    expect(reply && reply->kind == Reply::Kind::Error && reply->type == "auth_error", "an error");
    expect(failureText(*reply) == "Sorry, that didn’t work. Please try again.", "says a wrong password as GDM does");
    reply = parseReply(
        R"({"type":"error","error_type":"auth_error","description":"pam_acct_mgmt: NEW_AUTHTOK_REQD"})");
    expect(reply && failureText(*reply).starts_with("Your password has expired."),
        "tells a password that ran out from a wrong one");
    reply = parseReply(R"({"type":"error","error_type":"error","description":""})");
    expect(reply && failureText(*reply) == "Logging in failed", "says something for an error without words");
    reply = parseReply(R"({"type":"auth_message","auth_message_type":"secret","auth_message":"Password: "})");
    expect(reply && reply->kind == Reply::Kind::Question && reply->text == "Password: ", "a secret question");
    reply = parseReply(R"({"type":"auth_message","auth_message_type":"visible","auth_message":"Code"})");
    expect(reply && reply->kind == Reply::Kind::Question && reply->type == "visible", "a visible question");
    reply = parseReply(R"({"type":"auth_message","auth_message_type":"info","auth_message":"Hi"})");
    expect(reply && reply->kind == Reply::Kind::Message, "a message");
    expect(!parseReply(R"({"type":"what"})"), "refuses what it does not know");
}

void testSessions()
{
    Session session;
    session.id = "tde";
    const char* tde = "[Desktop Entry]\nName=TDE\nName[sl]=TDE po slovensko\nComment=x\n"
                      "Exec=tde-session %U\nType=Application\nDesktopNames=TDE;GNOME;\n"
                      "[Other Group]\nName=Not this\n";
    expect(parseSession(tde, "en_US.UTF-8", session), "reads a session");
    expect(session.name == "TDE", "names it");
    expect(session.exec == "tde-session", "takes the field codes out of Exec");
    expect(session.desktopNames.size() == 2 && session.desktopNames[1] == "GNOME", "reads the desktop names");
    expect(parseSession(tde, "sl_SI.UTF-8", session) && session.name == "TDE po slovensko", "names it in the language");
    expect(!parseSession("[Desktop Entry]\nName=X\nExec=x\nHidden=true\n", "", session), "leaves hidden ones out");
    expect(!parseSession("[Desktop Entry]\nName=X\nExec=x\nTryExec=/no/such/program\n", "", session),
        "leaves out those that cannot run");

    Session wayland {.id = "tde", .name = "TDE", .exec = "tde-session", .desktopNames = {"TDE"}, .x11 = false};
    const auto command = sessionCommand(wayland, "/bin/zsh");
    expect(command.size() == 4 && command[0] == "/bin/zsh" && command[1] == "-l" && command[3] == "exec tde-session",
        "runs a session through the login shell");
    expect(commandLine(command) == R"('/bin/zsh' '-l' '-c' 'exec tde-session')",
        "keeps the command whole when greetd joins it for /bin/sh");
    const auto environment = sessionEnvironment(wayland);
    expect(std::ranges::contains(environment, std::string("XDG_SESSION_TYPE=wayland")), "says the session type");
    expect(std::ranges::contains(environment, std::string("XDG_CURRENT_DESKTOP=TDE")), "names the desktop");
    expect(std::ranges::contains(environment, std::string("XDG_SESSION_DESKTOP=tde")), "names the session");

    Session x {.id = "it's", .name = "X", .exec = "run it's", .desktopNames = {}, .x11 = true};
    const auto xCommand = sessionCommand(x, "");
    expect(xCommand[0] == "/bin/sh" && xCommand[3].starts_with("exec startx /")
            && xCommand[3].ends_with("/tde-charon --x11-session") && xCommand[3].find(' ', 12) == xCommand[3].rfind(' '),
        "starts X for an X session, with no words that startx would split");
    const auto xEnvironment = sessionEnvironment(x);
    expect(std::ranges::contains(xEnvironment, std::string("XDG_SESSION_TYPE=x11")), "says it is X11");
    expect(std::ranges::contains(xEnvironment, std::string("TDE_X11_SESSION=run it's")),
        "leaves the X session's command whole in the environment");
}

void testUsers()
{
    expect(realNameOf("Žan Skamljič,,,") == "Žan Skamljič", "takes the name of GECOS");
    expect(realNameOf("") == "", "has none without one");
    expect(loginShell("/bin/zsh") && !loginShell("/usr/bin/nologin") && !loginShell("/bin/false"),
        "tells login shells from the others");

    Memory memory = Memory::parse("last zan\nsession zan tde\nsession guest gnome\nnonsense\n");
    expect(memory.lastUser() == "zan", "remembers who logged in last");
    expect(memory.sessionOf("zan") == "tde" && memory.sessionOf("guest") == "gnome", "remembers their sessions");
    expect(memory.sessionOf("nobody").empty(), "knows none for others");
    memory.loggedIn("guest", "tde");
    expect(memory.lastUser() == "guest" && memory.sessionOf("guest") == "tde", "remembers a new login");
    expect(Memory::parse(memory.text()).sessionOf("zan") == "tde", "reads back what it wrote");
    memory.loggedIn("two words", "tde");
    expect(memory.lastUser() == "guest", "forgets names it could not read back");
}

} // namespace

int main()
{
    testJson();
    testReplies();
    testSessions();
    testUsers();
    if (failures == 0)
        std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
