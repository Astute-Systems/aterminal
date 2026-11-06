#include "terminal.h"
#include <iostream>
#include <random>
#include <string>

// Feeds random bytes and escape fragments to the parser, resizing throughout and
// reading every cell. Terminal output is untrusted, so nothing here may crash it.
// Most useful in the CI sanitizer build.
int main() {
    constexpr unsigned kSeeds = 300;
    const std::string fragments[] = {
        "\033[", "\033]", "\033P", "\033(0", "\033)0", "\033(B", "\033#", "\033%", "\033 ", "\0337", "\0338", "\033H",
        "\033c", "\033M", "\033D", "\033E", "\033[?1049h", "\033[?1049l", "\033[?47h", "\033[?6h", "\033[?6l", "\033[?7l",
        "\033[?25l", "\033[4h", "\033[4l", "\033[!p", "\033[3g", "\033[g", "\033[0;0r", "\033[5;2r", "\033[2;5r", "\033[r",
        "\033[999;999H", "\033[99999999999999999999A", "\033[1b", "\033[500b", "\033[99999b", "\033[Z", "\033[999Z",
        "\033[999@", "\033[999P", "\033[999L", "\033[999M", "\033[999X", "\033[999S", "\033[999T", "\033[6n", "\033[c",
        "\033[>c", "\033[38;5;999m", "\033[38;2;1;2;3m", "\033[48;5m", "\033[38;2m", "\033[;;;;;m", "\xf0\x9f\x98\x80",
        "\xe4\xb8\x96", "\xcc\x81", "\xe2\x80\x8d", "\xed\xa0\x80", "\xff", "\x0e", "\x0f", "\t", "\b", "\r", "\n", "\x7f"};
    const std::size_t count = sizeof(fragments) / sizeof(*fragments);
    for (unsigned seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 random(seed);
        Terminal terminal(1 + random() % 30, 2 + random() % 90);
        terminal.SetHistoryLimit(random() % 3 == 0 ? 0 : random() % 50);
        for (int chunk = 0; chunk < 40; ++chunk) {
            std::string input;
            for (unsigned item = 1 + random() % 120; item > 0; --item) {
                switch (random() % 5) {
                case 0: input.push_back(static_cast<char>(random() & 255)); break;
                case 1: case 2: input += fragments[random() % count]; break;
                case 3:
                    input += "\033[" + std::to_string(random() % (random() % 2 ? 12 : 100000)) + (random() % 2 ? ";" : "") +
                             std::to_string(random() % 200) + static_cast<char>(0x40 + random() % 63);
                    break;
                default: input.push_back(static_cast<char>(32 + random() % 95));
                }
            }
            terminal.Feed(input);
            terminal.TakeReply();
            terminal.TakeBell();
            if (random() % 6 == 0) terminal.Resize(static_cast<int>(random() % 60) - 5, static_cast<int>(random() % 120) - 5);
            std::size_t cells = 0;
            for (int row = 0; row < terminal.Rows(); ++row)
                for (int column = 0; column < terminal.Columns(); ++column) cells += terminal.At(row, column).text.size();
            for (const auto& line : terminal.History()) cells += line.size();
            if (cells == static_cast<std::size_t>(-1)) return 1;
        }
    }
    std::cout << "Parser survived " << kSeeds << " seeds of hostile input\n";
}
