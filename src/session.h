#pragma once

#include "terminal.h"
#include <string>
#include <sys/types.h>
#include <vector>

// A shell command that runs `command`, then continues as an interactive `shell`.
std::string ThenInteractiveShell(const std::string& command, const std::string& shell, bool login = false);

bool IsBash(const std::string& shell);

struct ShellOptions {
    std::string startup;           // Run once, then continue interactively
    std::string prompt;            // A style from prompt.h; bash only
    std::string directory;         // Working directory, if set
    std::vector<std::string> exec; // Run this program instead of the shell
    bool login = false;
};

class Session {
public:
    Terminal terminal;
    explicit Session(int rows, int columns, const std::string& shell, const std::string& command = {},
                     const ShellOptions& options = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool Pump();
    void Send(std::string_view bytes);
    void Resize(int rows, int columns);
    bool Running() const { return child_ > 0; }
    int ExitStatus() const { return exit_status_; }
    int Scrollback() const { return scrollback_; }
    void Scroll(int lines);
    void Follow() { scrollback_ = 0; }

private:
    int master_ = -1;
    pid_t child_ = -1;
    int exit_status_ = 0;
    int scrollback_ = 0;
    std::string pending_;
    void Flush();
};