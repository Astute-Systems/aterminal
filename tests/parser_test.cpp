#include "terminal.h"
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        Terminal terminal(24, 80);
        terminal.Feed("\033[2J\033[3;5HHello \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x98\x80");
        Check(terminal.At(2, 4).text == U"H", "ANSI cursor placement");
        Check(terminal.At(2, 10).text == U"\u4e16" && terminal.At(2, 10).width == 2, "CJK width");
        Check(terminal.At(2, 15).text == U"\U0001f600", "emoji decoding");
        terminal.Feed("\033[1;1H\033[31;1mRed\033[0m");
        Check(terminal.At(0, 0).foreground == 1 && terminal.At(0, 0).bold, "SGR colors");
        terminal.Feed("\033[1;1H\033[1;3mAB\033[22mC\033[23mD");
        Check(terminal.At(0, 0).bold && terminal.At(0, 0).italic, "SGR bold italic");
        Check(!terminal.At(0, 2).bold && terminal.At(0, 2).italic, "SGR bold reset");
        Check(!terminal.At(0, 3).bold && !terminal.At(0, 3).italic, "SGR italic reset");
        terminal.Feed("\033[1;1H\033[31;1mRed\033[0m");
        terminal.Feed("\033[?1049hAlternate\033[?1049l");
        Check(terminal.At(0, 0).text == U"R", "alternate screen restore");
        terminal.Feed("\033[5;1H\033(0lqqk\033(B");
        Check(terminal.At(4, 0).text == U"\u250c", "DEC graphics");
        terminal.Feed("\033[6;1He\xcc");
        terminal.Feed("\x81");
        Check(terminal.At(5, 0).text == U"e\u0301", "split UTF-8 combining mark");
        terminal.Feed("\033[7;1H\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
        Check(terminal.At(6, 0).text == U"\U0001f469\u200d\U0001f4bb" && terminal.Column() == 2, "ZWJ cluster");
        terminal.Feed("\033[999999999999999999999999999999999;999999H!");
        Check(terminal.Row() < 24 && terminal.Column() < 80, "oversized CSI bounds");
        Terminal small(3, 5);
        small.Feed("one\r\ntwo\r\nthree\r\nfour");
        Check(!small.History().empty(), "scrollback");
        small.Resize(5, 12);
        Check(small.Rows() == 5 && small.Columns() == 12, "resize");
        small.Feed("\033[2J\033[H\033[38;2;12;34;56mX");
        Check(small.At(0, 0).foreground == 0x1000000 + 0x0c2238, "truecolor");
        Terminal editing(4, 10);
        editing.Feed("abcdef\033[1;3H\033[2P");
        Check(editing.At(0, 2).text == U"e", "delete characters");
        editing.Feed("\033[2@XY");
        Check(editing.At(0, 2).text == U"X" && editing.At(0, 4).text == U"e", "insert characters");
        editing.Feed("\033[2;3r\033[3;1Hbottom\n");
        Check(editing.At(0, 0).text == U"a" && editing.At(1, 0).text == U"b", "scroll region");
        editing.Feed("\033[6n");
        Check(editing.TakeReply() == "\033[3;7R", "cursor query reply");
        editing.Feed("\033[?2004h\033[?1h");
        Check(editing.BracketedPaste() && editing.ApplicationCursor(), "input modes");
        editing.Feed("\033]2;hello\033\\");
        Check(editing.Title() == "hello", "OSC title");
        editing.Feed("\033[?1049h\033[2;5Hwide\033[?1049l");
        editing.Resize(2, 4);
        editing.Feed("\033[?1049h\033[?1049l");
        Check(editing.Rows() == 2 && editing.Columns() == 4, "alternate-screen resize");
        Terminal wide(3, 5);
        wide.Feed("\xe4\xb8\x96\033[1Gx");
        Check(wide.At(0, 1).width == 1, "overwritten wide glyph continuation");
        wide.Feed("\033[1G\xe4\xb8\x96\033[2Gx");
        Check(wide.At(0, 0).text == U" ", "overwritten wide glyph leader");
        wide.Feed("\033[HabcdeX");
        Check(wide.At(1, 0).text == U"X", "delayed wrap");
        wide.Feed("\033[H\xc0\xaf\xed\xa0\x80");
        Check(wide.At(0, 0).text == U"\ufffd", "invalid UTF-8 replacement");
        std::uint32_t random = 17;
        for (int iteration = 0; iteration < 20000; ++iteration) {
            random = random * 1664525u + 1013904223u;
            const char byte = static_cast<char>(random >> 24);
            wide.Feed(std::string_view(&byte, 1));
            if (iteration % 251 == 0) wide.Resize(1 + (random % 8), 2 + (random % 24));
            Check(wide.Row() >= 0 && wide.Row() < wide.Rows() && wide.Column() >= 0 && wide.Column() < wide.Columns(), "fuzzed cursor bounds");
        }
        std::cout << "ANSI, UTF-8, emoji, alternate screen, DEC graphics, scrollback and resize passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}