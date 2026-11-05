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
        Session startup(24, 80, "/bin/sh", ThenInteractiveShell("printf 'RAN%s\\n' ONCE # comment", "/bin/sh"));
        Until(startup, [&] { return Text(startup.terminal).find("RANONCE") != std::string::npos; });
        startup.Send("printf 'STILL%s\\n' INTERACTIVE\n");
        Until(startup, [&] { return Text(startup.terminal).find("STILLINTERACTIVE") != std::string::npos; });
        if (!startup.Running()) throw std::runtime_error("Shell exited after the startup command");
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
            std::ofstream(rc) << "PS1='\\[\\033[01;32m\\]ab\\[\\033[00m\\]:cd\\$ '\nalias hello='printf ALIASED'\n";
            std::ofstream(std::string(home) + "/.profile") << "printf 'LOGIN%s\\n' RAN\n";
            setenv("HOME", home, 1);
            Session prompt(24, 80, "/bin/bash");
            Until(prompt, [&] { return Text(prompt.terminal).find("ab:cd$ ") != std::string::npos; });
            int row = 0;
            while (Terminal::Utf8(prompt.terminal.At(row, 0).text) != "a" || Terminal::Utf8(prompt.terminal.At(row, 5).text) != "$") ++row;
            for (int column = 0; column < 5; ++column)
                if (!prompt.terminal.At(row, column).bold) throw std::runtime_error("Prompt is not bold up to the dollar sign");
            if (prompt.terminal.At(row, 5).bold) throw std::runtime_error("Bold continued past the prompt");
            prompt.Send("cd /proc/$$/fd; echo FDS= *\n");
            Until(prompt, [&] { return Text(prompt.terminal).find("\nFDS=") != std::string::npos; });
            if (Text(prompt.terminal).find("\nFDS= 0 1 2 255") == std::string::npos)
                throw std::runtime_error("Bash startup pipe stayed open: " + Text(prompt.terminal));
            ShellOptions alias_options;
            alias_options.startup = "hello";
            Session aliased(24, 80, "/bin/bash", {}, alias_options);
            Until(aliased, [&] { return Text(aliased.terminal).find("ALIASED") != std::string::npos; });
            Until(aliased, [&] { return Text(aliased.terminal).find("ab:cd$ ") != std::string::npos; });
            ShellOptions style_options;
            style_options.prompt = "redhat";
            style_options.directory = "/usr";
            Session styled(24, 80, "/bin/bash", {}, style_options);
            Until(styled, [&] { return Text(styled.terminal).find(" usr]$ ") != std::string::npos; });
            if (!styled.terminal.At(0, 0).bold) throw std::runtime_error("Prompt style is not bold");
            style_options.prompt = "dos";
            Session dos(24, 80, "/bin/bash", {}, style_options);
            Until(dos, [&] { return Text(dos.terminal).find("C:\\USR>") != std::string::npos; });
            style_options.prompt = "unix";
            Session plain(24, 80, "/bin/bash", {}, style_options);
            Until(plain, [&] { return Text(plain.terminal).find("$ ") == 0; });
            style_options.prompt = "O'K>";
            Session custom(24, 80, "/bin/bash", {}, style_options);
            Until(custom, [&] { return Text(custom.terminal).find("O'K> ") != std::string::npos; });
            ShellOptions login_options;
            login_options.login = true;
            Session login(24, 80, "/bin/bash", {}, login_options);
            Until(login, [&] { return Text(login.terminal).find("LOGINRAN") != std::string::npos; });
            std::filesystem::remove_all(home);
        }
        ShellOptions direct;
        direct.exec = {"printf", "EXEC%s\\n", "DIRECT"};
        direct.directory = "/usr";
        Session program(24, 80, "/bin/sh", {}, direct);
        Until(program, [&] { return !program.Running(); });
        program.Pump();
        if (Text(program.terminal).find("EXECDIRECT") == std::string::npos) throw std::runtime_error("Direct program did not run");
        ShellOptions in_directory;
        in_directory.directory = "/usr";
        Session located(24, 80, "/bin/sh", "pwd", in_directory);
        Until(located, [&] { return !located.Running(); });
        located.Pump();
        if (Text(located.terminal).find("/usr") == std::string::npos) throw std::runtime_error("Working directory was not applied");
        std::cout << "PTY input, output, resize, exit, session isolation, control characters, scrollback, prompts and shell options passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}