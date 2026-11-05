#include "motd.h"

#include <random>
#include <stdexcept>

#define TITLE(text) "\033[1m" text "\033[0m\n\n"

const std::vector<Motd>& Motds() {
    static const std::vector<Motd> messages{
        {"welcome", "A plain system login banner",
         TITLE("ATERMINAL  /  PHOSPHOR WORKSTATION")
         "  All twelve consoles are standing by on F1-F12.\n"
         "  Unauthorized boredom is strictly prohibited.\n\n"},
        {"tips", "Useful keyboard shortcuts",
         TITLE("OPERATOR QUICK REFERENCE")
         "  F1-F12               switch console\n"
         "  Ctrl+Shift+P         setup panel\n"
         "  Ctrl+Shift+A / G     amber / green phosphor\n"
         "  Ctrl+Shift+R         toggle CRT effects\n"
         "  Shift+PageUp/Down    scrollback\n"
         "  Alt+Enter            fullscreen\n\n"},
        {"sysop", "Notice from an overworked system operator",
         TITLE("NOTICE FROM THE SYSOP")
         "  Backups run at 0300. Restores run on hope.\n"
         "  Disk quotas are enforced by guilt. Please be considerate.\n"
         "  The tape drive is not a cup holder. This means you.\n\n"},
        {"phosphor", "Care instructions for your CRT",
         TITLE("PHOSPHOR CARE ADVISORY")
         "  Tube warm-up complete. Convergence is within tolerance.\n"
         "  Do not leave one prompt on screen for a decade.\n"
         "  Burn-in is permanent. So is that command you just typed.\n\n"},
        {"unix", "Workshop rules for the command line",
         TITLE("RULES OF THE WORKSHOP")
         "  1. Small tools, sharp edges.\n"
         "  2. Text is the universal adapter.\n"
         "  3. Read the man page, then read it again.\n"
         "  4. Look twice before rm -rf.\n\n"},
        {"haiku", "A short poem for the night shift",
         TITLE("NIGHT SHIFT HAIKU")
         "    Green glow, midnight hum.\n"
         "    The cursor blinks, patient, calm.\n"
         "    Make finishes. Dawn.\n\n"},
        {"bbs", "Dial-up bulletin board greeting",
         TITLE("CONNECT 2400 / WELCOME TO THE BOARD")
         "  Caller number 000001. It has been a quiet year.\n"
         "  Messages waiting: 0.  Doors open: 12.\n"
         "  Please do not tie up the line. Mum needs the phone.\n\n"},
        {"launch", "Pre-flight checklist before you start work",
         TITLE("PRE-FLIGHT CHECKLIST")
         "  [x] Coffee          nominal\n"
         "  [x] Shell           responding\n"
         "  [x] Git status      clean, allegedly\n"
         "  [ ] Plan            pending\n\n"
         "  Go for launch.\n\n"},
    };
    return messages;
}

bool MotdExists(const std::string& name) {
    if (name == "random") return true;
    for (const Motd& message : Motds())
        if (name == message.name) return true;
    return false;
}

std::string MotdText(const std::string& name) {
    const auto& messages = Motds();
    const char* text = nullptr;
    if (name == "random") {
        static std::mt19937 generator{std::random_device{}()};
        text = messages[std::uniform_int_distribution<std::size_t>(0, messages.size() - 1)(generator)].text;
    } else {
        for (const Motd& message : messages)
            if (name == message.name) text = message.text;
    }
    if (!text) throw std::invalid_argument("Unknown MOTD: " + name);
    std::string result;
    for (const char* cursor = text; *cursor; ++cursor) {
        if (*cursor == '\n') result += '\r';
        result += *cursor;
    }
    return result;
}
