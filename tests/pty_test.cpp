#include "session.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

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
        Session scrolled(24, 80, "/bin/sh", "stty -echo; seq 1 100; read a; seq 1 10; read b; printf '\\033[3J'; read c; seq 1 100; read d; printf '\\033[?1049h'; read e");
        Until(scrolled, [&] { return scrolled.terminal.History().size() >= 77; });
        scrolled.Scroll(23);
        scrolled.Scroll(23);
        if (scrolled.Scrollback() != 46) throw std::runtime_error("Scrollback paging failed");
        scrolled.Scroll(-23);
        if (scrolled.Scrollback() != 23) throw std::runtime_error("Scrollback paging down failed");
        const auto history = scrolled.terminal.History().size();
        scrolled.Send("\n");
        Until(scrolled, [&] { return scrolled.terminal.History().size() >= history + 10; });
        if (scrolled.Scrollback() != 33) throw std::runtime_error("Scrolled view drifted with new output");
        scrolled.Send("\n");
        Until(scrolled, [&] { return scrolled.terminal.History().empty(); });
        if (scrolled.Scrollback() != 0) throw std::runtime_error("Scrollback offset survived clearing history");
        scrolled.Send("\n");
        Until(scrolled, [&] { return scrolled.terminal.History().size() >= 77; });
        scrolled.Scroll(10);
        scrolled.Send("\n");
        Until(scrolled, [&] { return scrolled.terminal.Alternate(); });
        if (scrolled.Scrollback() != 0) throw std::runtime_error("Scrollback offset survived the alternate screen");
        if (access("/bin/bash", X_OK) == 0) {
            char home[] = "/tmp/aterminal-pty-XXXXXX";
            if (!mkdtemp(home)) throw std::runtime_error("mkdtemp failed");
            const std::string rc = std::string(home) + "/.bashrc";
            std::ofstream(rc) << "PS1='\\[\\033[01;32m\\]ab\\[\\033[00m\\]:cd\\$ '\n";
            setenv("HOME", home, 1);
            Session prompt(24, 80, "/bin/bash");
            Until(prompt, [&] { return Text(prompt.terminal).find("ab:cd$ ") != std::string::npos; });
            std::filesystem::remove_all(home);
            int row = 0;
            while (Terminal::Utf8(prompt.terminal.At(row, 0).text) != "a" || Terminal::Utf8(prompt.terminal.At(row, 5).text) != "$") ++row;
            for (int column = 0; column < 5; ++column)
                if (!prompt.terminal.At(row, column).bold) throw std::runtime_error("Prompt is not bold up to the dollar sign");
            if (prompt.terminal.At(row, 5).bold) throw std::runtime_error("Bold continued past the prompt");
            prompt.Send("cd /proc/$$/fd; echo FDS= *\n");
            Until(prompt, [&] { return Text(prompt.terminal).find("\nFDS=") != std::string::npos; });
            if (Text(prompt.terminal).find("\nFDS= 0 1 2 255") == std::string::npos)
                throw std::runtime_error("Bash startup pipe stayed open: " + Text(prompt.terminal));
        }
        std::cout << "PTY input, output, resize, exit, session isolation, control characters, scrollback and bold prompt passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}