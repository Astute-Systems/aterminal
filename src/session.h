#pragma once

#include "terminal.h"
#include <string>
#include <sys/types.h>

// A shell command that runs `command`, then continues as an interactive `shell`.
std::string ThenInteractiveShell(const std::string& command, const std::string& shell);

class Session {
public:
    Terminal terminal;
    explicit Session(int rows, int columns, const std::string& shell, const std::string& command = {});
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