#include "motd.h"
#include "terminal.h"
#include <iostream>
#include <set>
#include <stdexcept>

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Screen(const Terminal& terminal) {
    std::string result;
    for (int row = 0; row < terminal.Rows(); ++row) {
        for (int column = 0; column < terminal.Columns(); ++column)
            result += Terminal::Utf8(terminal.At(row, column).text);
        result += '\n';
    }
    return result;
}

int main() {
    try {
        Check(Motds().size() >= 5, "several messages are built in");
        std::set<std::string> texts;
        for (const Motd& message : Motds()) {
            const std::string name = message.name;
            Check(MotdExists(name), name + " is recognised");
            Check(name != "random" && name != "list", name + " is a reserved name");
            const std::string text = MotdText(name);
            Check(texts.insert(text).second, name + " is a distinct message");
            Check(text.size() >= 4 && text.compare(text.size() - 4, 4, "\r\n\r\n") == 0, name + " leaves a gap before the prompt");

            // Every line fits, and the whole message stays on, the default 80x25 screen.
            int rows = 1;
            std::size_t start = 0;
            for (std::size_t end; (end = text.find("\r\n", start)) != std::string::npos; start = end + 2, ++rows) {
                const std::string line = text.substr(start, end - start);
                std::size_t visible = line.size();
                for (const char* code : {"\033[1m", "\033[0m"})
                    if (line.find(code) != std::string::npos) visible -= 4;
                Check(visible <= 76, name + " fits 80 columns");
            }
            Check(rows <= 20, name + " leaves room for the prompt");

            Terminal terminal(25, 80);
            terminal.Feed(text);
            Check(terminal.At(0, 0).bold, name + " has a bold title");
            Check(terminal.Column() == 0, name + " returns to column one");
            Check(Screen(terminal).find(text.substr(text.find("m") + 1, 4)) != std::string::npos, name + " reaches the screen");
        }
        Check(MotdExists("random"), "random is accepted");
        Check(!MotdExists("nonsense") && !MotdExists(""), "unknown names are rejected");
        Check(texts.count(MotdText("random")) == 1, "random selects a built-in message");
        std::set<std::string> picks;
        for (int attempt = 0; attempt < 200; ++attempt) picks.insert(MotdText("random"));
        Check(picks.size() > 1, "random varies between calls");
        bool threw = false;
        try { MotdText("nonsense"); } catch (const std::invalid_argument&) { threw = true; }
        Check(threw, "unknown text lookup throws");
        std::cout << "MOTD selection, formatting and rendering passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
