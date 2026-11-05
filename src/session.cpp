#include "session.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

// Passed to bash in place of ~/.bashrc: runs that as usual, then removes its colour
// resets so bold carries through to the final "\$ ". The "$" stays normal weight.
constexpr char kBashStartup[] =
    R"([ -f ~/.bashrc ] && . ~/.bashrc
if [[ $PS1 == *'\$ ' ]]; then
    PS1=${PS1//'\[\033[00m\]'/}
    PS1='\[\033[1m\]'${PS1%'\$ '}'\[\033[0m\]\$ '
fi
)";

bool IsBash(const std::string& shell) {
    return shell.substr(shell.rfind('/') + 1) == "bash";
}

}

std::string ThenInteractiveShell(const std::string& command, const std::string& shell) {
    std::string quoted = "'";
    for (char byte : shell) {
        if (byte == '\'') quoted += "'\\''";
        else quoted += byte;
    }
    // A newline, not a semicolon, so a trailing comment in the command stays harmless.
    return command + "\nexec " + quoted + "' -i";
}

Session::Session(int rows, int columns, const std::string& shell, const std::string& command)
    : terminal(rows, columns) {
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; ++entry) {
        const std::string value(*entry);
        if (value.rfind("TERM=", 0) && value.rfind("COLORTERM=", 0) &&
            value.rfind("COLUMNS=", 0) && value.rfind("LINES=", 0)) environment.push_back(value);
    }
    environment.emplace_back("TERM=xterm-256color");
    environment.emplace_back("COLORTERM=truecolor");
    std::vector<char*> env;
    for (auto& entry : environment) env.push_back(entry.data());
    env.push_back(nullptr);
    std::vector<std::string> arguments{shell};
    int startup[2] = {-1, -1};
    if (command.empty() && IsBash(shell) && pipe(startup) == 0) {
        // Far smaller than a pipe buffer, so this cannot block; bash reads it at start-up.
        const std::string script = kBashStartup + ("exec " + std::to_string(startup[0]) + "<&-\n");
        const auto ignored = write(startup[1], script.data(), script.size());
        (void)ignored;
        close(startup[1]);
        arguments.emplace_back("--rcfile");
        arguments.push_back("/dev/fd/" + std::to_string(startup[0]));
    }
    if (command.empty()) arguments.emplace_back("-i");
    else { arguments.emplace_back("-c"); arguments.push_back(command); }
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    winsize size{};
    size.ws_row = static_cast<unsigned short>(terminal.Rows());
    size.ws_col = static_cast<unsigned short>(terminal.Columns());
    child_ = forkpty(&master_, nullptr, nullptr, &size);
    if (child_ != 0 && startup[0] >= 0) close(startup[0]);
    if (child_ < 0) throw std::runtime_error(std::string("forkpty: ") + std::strerror(errno));
    if (child_ == 0) {
        execve(shell.c_str(), argv.data(), env.data());
        constexpr char failure[] = "aterminal: cannot execute shell\r\n";
        const auto ignored = write(STDERR_FILENO, failure, sizeof(failure) - 1);
        (void)ignored;
        _exit(127);
    }
    fcntl(master_, F_SETFD, FD_CLOEXEC);
    fcntl(master_, F_SETFL, fcntl(master_, F_GETFL) | O_NONBLOCK);
}

Session::~Session() {
    if (master_ >= 0) close(master_);
    if (child_ > 0) {
        kill(-child_, SIGHUP);
        kill(child_, SIGKILL);
        while (waitpid(child_, nullptr, 0) < 0 && errno == EINTR) {}
    }
}

void Session::Flush() {
    while (master_ >= 0 && !pending_.empty()) {
        const ssize_t sent = write(master_, pending_.data(), pending_.size());
        if (sent > 0) pending_.erase(0, static_cast<std::size_t>(sent));
        else if (sent < 0 && errno == EINTR) continue;
        else break;
    }
}

void Session::Send(std::string_view bytes) {
    if (master_ < 0 || !Running()) return;
    if (pending_.size() + bytes.size() > 4 * 1024 * 1024) return;
    pending_.append(bytes);
    Flush();
}

bool Session::Pump() {
    bool changed = false;
    const int history_before = static_cast<int>(terminal.History().size());
    char buffer[16384];
    for (int batch = 0; master_ >= 0 && batch < 16; ++batch) {
        const ssize_t count = read(master_, buffer, sizeof(buffer));
        if (count > 0) {
            terminal.Feed(std::string_view(buffer, static_cast<std::size_t>(count)));
            changed = true;
        } else if (count < 0 && errno == EINTR) continue;
        else if (count == 0 || (count < 0 && errno == EIO)) {
            close(master_); master_ = -1; pending_.clear(); break;
        } else break;
    }
    if (changed) {
        // Keep a scrolled view on the same lines as output arrives; a full-screen
        // application has no scrollback, so return to the live screen for it.
        const int history = static_cast<int>(terminal.History().size());
        if (terminal.Alternate()) scrollback_ = 0;
        else if (scrollback_ > 0) scrollback_ = std::clamp(scrollback_ + history - history_before, 0, history);
        Send(terminal.TakeReply());
    }
    Flush();
    if (child_ > 0) {
        int status = 0;
        const pid_t result = waitpid(child_, &status, WNOHANG);
        if (result == child_) {
            exit_status_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            child_ = -1;
            changed = true;
        }
    }
    return changed;
}

void Session::Resize(int rows, int columns) {
    if (rows == terminal.Rows() && columns == terminal.Columns()) return;
    terminal.Resize(rows, columns);
    if (master_ >= 0) {
        winsize size{};
        size.ws_row = static_cast<unsigned short>(terminal.Rows());
        size.ws_col = static_cast<unsigned short>(terminal.Columns());
        ioctl(master_, TIOCSWINSZ, &size);
    }
}

void Session::Scroll(int lines) {
    if (terminal.Alternate()) return;
    scrollback_ = std::clamp(scrollback_ + lines, 0, static_cast<int>(terminal.History().size()));
}