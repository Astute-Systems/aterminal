#include "keys.h"
#include "terminal.h"
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string Key(SDL_Keycode key, Uint16 mod = 0, bool cursor = false, bool keypad = false) {
    return EncodeKey(key, mod, cursor, keypad);
}

int main() {
    try {
        Check(Key(SDLK_UP) == "\033[A" && Key(SDLK_UP, 0, true) == "\033OA", "arrow and application cursor keys");
        Check(Key(SDLK_UP, KMOD_SHIFT) == "\033[1;2A" && Key(SDLK_HOME, KMOD_CTRL) == "\033[1;5H", "modified arrows and Home");
        Check(Key(SDLK_END) == "\033[F" && Key(SDLK_END, 0, true) == "\033OF", "End key");

        Check(Key(SDLK_DELETE) == "\033[3~" && Key(SDLK_DELETE, KMOD_CTRL) == "\033[3;5~", "Ctrl+Delete");
        Check(Key(SDLK_PAGEUP, KMOD_ALT) == "\033[5;3~" && Key(SDLK_INSERT, KMOD_SHIFT) == "\033[2;2~", "modified Insert and PageUp");
        Check(Key(SDLK_PAGEDOWN, KMOD_CTRL | KMOD_SHIFT) == "\033[6;6~", "Ctrl+Shift+PageDown");

        Check(Key(SDLK_F1) == "\033OP" && Key(SDLK_F4, KMOD_SHIFT) == "\033[1;2S", "F1 to F4");
        Check(Key(SDLK_F5) == "\033[15~" && Key(SDLK_F12, KMOD_CTRL) == "\033[24;5~", "F5 to F12");
        Check(Key(SDLK_F13) == "\033[25~" && Key(SDLK_F20) == "\033[34~", "F13 to F20");

        Check(Key(SDLK_BACKSPACE) == "\x7f" && Key(SDLK_BACKSPACE, KMOD_CTRL) == "\x08", "Backspace and Ctrl+Backspace");
        Check(Key(SDLK_BACKSPACE, KMOD_ALT) == "\033\x7f", "Alt+Backspace");
        Check(Key(SDLK_TAB) == "\t" && Key(SDLK_TAB, KMOD_SHIFT) == "\033[Z" && Key(SDLK_TAB, KMOD_ALT) == "\033\t", "Tab");
        Check(Key(SDLK_ESCAPE, KMOD_ALT) == "\033\033" && Key(SDLK_RETURN, KMOD_ALT) == "\033\r", "Alt+Escape and Alt+Enter");

        Check(Key(SDLK_c, KMOD_CTRL) == "\x03" && Key(SDLK_c, KMOD_CTRL | KMOD_ALT) == "\033\x03", "Ctrl+letter");
        Check(Key(SDLK_2, KMOD_CTRL) == std::string(1, '\0') && Key(SDLK_3, KMOD_CTRL) == "\033", "Ctrl+2 and Ctrl+3");
        Check(Key(SDLK_6, KMOD_CTRL) == "\x1e" && Key(SDLK_7, KMOD_CTRL) == "\x1f" && Key(SDLK_8, KMOD_CTRL) == "\x7f", "Ctrl+6, 7 and 8");
        Check(Key(SDLK_SLASH, KMOD_CTRL) == "\x1f" && Key(SDLK_MINUS, KMOD_CTRL) == "\x1f", "Ctrl+/ and Ctrl+-");
        Check(Key(SDLK_LEFTBRACKET, KMOD_CTRL) == "\033" && Key(SDLK_SPACE, KMOD_CTRL) == std::string(1, '\0'), "Ctrl+[ and Ctrl+Space");

        Check(Key(SDLK_a).empty() && Key(SDLK_KP_5, KMOD_NUM).empty(), "text keys are left to text input");
        Check(Key(SDLK_KP_ENTER) == "\r" && Key(SDLK_KP_ENTER, 0, false, true) == "\033OM", "keypad Enter");
        Check(Key(SDLK_KP_8) == "\033[A" && Key(SDLK_KP_7) == "\033[H" && Key(SDLK_KP_1) == "\033[F", "NumLock off keypad navigation");
        Check(Key(SDLK_KP_0) == "\033[2~" && Key(SDLK_KP_PERIOD) == "\033[3~" && Key(SDLK_KP_9) == "\033[5~", "NumLock off keypad editing");
        Check(Key(SDLK_KP_5) == "\033[E" && Key(SDLK_KP_4, KMOD_SHIFT) == "\033[1;2D", "NumLock off keypad centre and modifiers");
        Check(Key(SDLK_KP_0, KMOD_NUM, false, true) == "\033Op" && Key(SDLK_KP_9, 0, false, true) == "\033Oy", "application keypad digits");
        Check(Key(SDLK_KP_PLUS, 0, false, true) == "\033Ol" && Key(SDLK_KP_MINUS, 0, false, true) == "\033Om", "application keypad operators");
        Check(Key(SDLK_KP_PERIOD, KMOD_NUM, false, true) == "\033On", "application keypad period");
        Check(KeypadTypesText(SDLK_KP_1, KMOD_NUM) && !KeypadTypesText(SDLK_KP_1, 0) && KeypadTypesText(SDLK_KP_PLUS, 0) &&
              !KeypadTypesText(SDLK_KP_ENTER, KMOD_NUM), "keypad keys that also produce text");

        Terminal terminal(24, 80);
        Check(!terminal.ApplicationKeypad() && !terminal.FocusReporting(), "keypad and focus modes start off");
        terminal.Feed("\033=\033[?1004h");
        Check(terminal.ApplicationKeypad() && terminal.FocusReporting(), "keypad and focus modes turn on");
        terminal.Feed("\033>\033[?1004l");
        Check(!terminal.ApplicationKeypad() && !terminal.FocusReporting(), "keypad and focus modes turn off");
        terminal.Feed("\033=\033[?1004h\033[!p");
        Check(!terminal.ApplicationKeypad() && terminal.FocusReporting(), "soft reset leaves the keypad normal");
        terminal.Feed("\033c");
        Check(!terminal.FocusReporting(), "full reset clears focus reporting");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
