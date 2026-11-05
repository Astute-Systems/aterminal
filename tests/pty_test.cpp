#include "session.h"

#include <chrono>
#include <iostream>
#include <poll.h>
#include <stdexcept>

std::string Text(const Terminal& terminal) {
    std::string result;
    for (int row = 0; row < terminal.Rows(); ++row) {
        for (int column = 0; column < terminal.Columns(); ++column)
            result += Terminal::Utf8(terminal.At(row, column).text);
        result += '\n';
    }
    return result;
}

template <typename Predicate> void Until(Session& session, Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        session.Pump();
        if (done()) return;
        poll(nullptr, 0, 5);
    }
    throw std::runtime_error("PTY timed out");
}

int main() {
    try {
        Session session(24, 80, "/bin/sh", "stty -echo; printf READY; read value; printf '\\nRESULT:%s\\n' \"$value\"; stty size");
        Until(session, [&] { return Text(session.terminal).find("READY") != std::string::npos; });
        session.Resize(30, 100);
        session.Send("hello\n");
        Until(session, [&] { return !session.Running(); });
        session.Pump();
        const std::string text = Text(session.terminal);
        if (text.find("RESULT:hello") == std::string::npos || text.find("30 100") == std::string::npos || session.ExitStatus() != 0)
            throw std::runtime_error("PTY input, output or resize failed: " + text);
        Session second(24, 80, "/bin/sh", "printf SECOND");
        Until(second, [&] { return !second.Running(); });
        second.Pump();
        if (Text(second.terminal).find("SECOND") == std::string::npos || Text(session.terminal).find("SECOND") != std::string::npos)
            throw std::runtime_error("Session isolation failed");
        Session controls(24, 80, "/bin/sh", "stty raw -echo; printf READY; dd bs=1 count=2 2>/dev/null | od -An -tu1 | tr -s ' '");
        Until(controls, [&] { return Text(controls.terminal).find("READY") != std::string::npos; });
        controls.Send("\x1a\x19");
        Until(controls, [&] { return !controls.Running(); });
        controls.Pump();
        if (Text(controls.terminal).find("26 25") == std::string::npos || controls.ExitStatus() != 0)
            throw std::runtime_error("Ctrl+Z/Y bytes were not preserved through the PTY");
        Session startup(24, 80, "/bin/sh", ThenInteractiveShell("printf 'RAN%s\\n' ONCE # comment", "/bin/sh"));
        Until(startup, [&] { return Text(startup.terminal).find("RANONCE") != std::string::npos; });
        startup.Send("printf 'STILL%s\\n' INTERACTIVE\n");
        Until(startup, [&] { return Text(startup.terminal).find("STILLINTERACTIVE") != std::string::npos; });
        if (!startup.Running()) throw std::runtime_error("Shell exited after the startup command");
        std::cout << "PTY input, output, resize, exit, session isolation and control characters passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}