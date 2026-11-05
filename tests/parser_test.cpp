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
        {
            Terminal margins(10, 20);
            margins.Feed("\033[3;6r\033[5;1H\033[9B");
            Check(margins.Row() == 5, "cursor down stops at the bottom margin");
            margins.Feed("\033[5;1H\033[9A");
            Check(margins.Row() == 2, "cursor up stops at the top margin");
            margins.Feed("\033[9;1H\033[9B");
            Check(margins.Row() == 9, "cursor below the region moves to the screen edge");
            margins.Feed("\033[2;1H\033[9A");
            Check(margins.Row() == 0, "cursor above the region moves to the screen edge");
            margins.Feed("\033[?6h\033(0\0337\033[?6l\033(B\033[?7l\0338");
            margins.Feed("\033[1;1Hq\033(B");
            Check(margins.Row() == 2 && margins.At(2, 0).text == U"\u2500", "restore cursor brings back origin mode and charset");
            margins.Feed("\033[?6l\033[?7h\033[1;1H\0337\033[?7l\0338\033[1;20Hab");
            Check(margins.At(1, 0).text == U"b", "restore cursor brings back autowrap");
            Terminal sets(4, 20);
            sets.Feed("\033)0a\x0eq\x0fq\033(0q\033(B");
            Check(sets.At(0, 0).text == U"a" && sets.At(0, 1).text == U"\u2500", "SO selects G1");
            Check(sets.At(0, 2).text == U"q" && sets.At(0, 3).text == U"\u2500", "SI selects G0");
            sets.Feed("\033[2;1H\033%Gx\033*0y\033#3z");
            Check(sets.At(1, 0).text == U"x" && sets.At(1, 1).text == U"y" && sets.At(1, 2).text == U"z", "two-byte escapes print nothing");
            sets.Feed("\033[3;1H\033[>4;2mp\033[2mq\033[22mr");
            Check(!sets.At(2, 0).underline && !sets.At(2, 0).dim, "modifier key mode is not SGR");
            Check(sets.At(2, 1).dim && !sets.At(2, 2).dim, "SGR dim");
            sets.Feed("\033[4;1Habc\033[4;1H\033[1 @");
            Check(sets.At(3, 0).text == U"a", "intermediates do not fall through");
            sets.Feed("\033[1;3r\033[?6h\033[4h\033[1m\033(0\033[!p\033[1;1Hq");
            Check(sets.At(0, 0).text == U"q" && !sets.At(0, 0).bold, "soft reset clears the pen and charset");
            sets.Feed("\033[>c");
            Check(sets.TakeReply() == "\033[>0;0;0c", "secondary device attributes");
        }
        {
            Terminal repeat(4, 10);
            repeat.Feed("-\033[3b+\033[2;1H\033(0q\033[4bx\033(B");
            Check(repeat.At(0, 3).text == U"-" && repeat.At(0, 4).text == U"+", "repeat preceding character");
            Check(repeat.At(1, 4).text == U"\u2500" && repeat.At(1, 5).text == U"\u2502", "repeat DEC line drawing");
            repeat.Feed("\033[3;9Hab\033[2bc");
            Check(repeat.At(3, 1).text == U"b" && repeat.At(3, 2).text == U"c", "repeat wraps at the margin");
            repeat.Feed("\033[4;1H\033[K\033[1;20H\033[Z");
            Check(repeat.Column() == 8, "back tab");
            repeat.Feed("\033[Z\033[Z");
            Check(repeat.Column() == 0, "back tab stops at the margin");
            repeat.Feed("\033[1;4H\033H\033[1;1H\t");
            Check(repeat.Column() == 3, "set tab stop");
            repeat.Feed("\033[3g\033[1;1H\t");
            Check(repeat.Column() == 9, "clear all tab stops");
            repeat.Feed("\033c\t");
            Check(repeat.Column() == 8, "reset restores tab stops");
        }
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