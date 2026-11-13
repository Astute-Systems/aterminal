#pragma once
#include <SDL.h>
#include <string>

// Translates keys that do not arrive as text into the bytes an xterm would send.
// Plain text comes from SDL_TEXTINPUT, so keys that only type a character return nothing.

inline int KeyModifier(Uint16 mod) {
    return 1 + ((mod & KMOD_SHIFT) ? 1 : 0) + ((mod & KMOD_ALT) ? 2 : 0) + ((mod & KMOD_CTRL) ? 4 : 0);
}

// The control character Ctrl sends with this key, or -1.
inline int ControlCode(SDL_Keycode key) {
    if (key >= SDLK_a && key <= SDLK_z) return static_cast<int>(key - SDLK_a + 1);
    switch (key) {
    case SDLK_SPACE: case SDLK_2: return 0x00;
    case SDLK_LEFTBRACKET: case SDLK_3: return 0x1b;
    case SDLK_BACKSLASH: case SDLK_4: return 0x1c;
    case SDLK_RIGHTBRACKET: case SDLK_5: return 0x1d;
    case SDLK_6: return 0x1e;
    case SDLK_7: case SDLK_SLASH: case SDLK_MINUS: return 0x1f;
    case SDLK_8: return 0x7f;
    default: return -1;
    }
}

// Whether SDL also sends a text event for this keypad key, which application keypad mode must drop.
inline bool KeypadTypesText(SDL_Keycode key, Uint16 mod) {
    switch (key) {
    case SDLK_KP_DIVIDE: case SDLK_KP_MULTIPLY: case SDLK_KP_MINUS: case SDLK_KP_PLUS: return true;
    case SDLK_KP_0: case SDLK_KP_1: case SDLK_KP_2: case SDLK_KP_3: case SDLK_KP_4:
    case SDLK_KP_5: case SDLK_KP_6: case SDLK_KP_7: case SDLK_KP_8: case SDLK_KP_9: case SDLK_KP_PERIOD:
        return (mod & KMOD_NUM) && !(mod & (KMOD_CTRL | KMOD_ALT));
    default: return false;
    }
}

inline std::string EncodeKey(SDL_Keycode key, Uint16 mod, bool application_cursor, bool application_keypad) {
    const bool shift = mod & KMOD_SHIFT, alt = mod & KMOD_ALT, control = mod & KMOD_CTRL;
    const int modifier = KeyModifier(mod);
    const std::string meta = alt ? "\033" : "";
    const auto number = [](int value) { return std::to_string(value); };

    if (control) {
        const int code = ControlCode(key);
        if (code >= 0) return meta + static_cast<char>(code);
    }

    if (application_keypad && !control && !alt) {
        char final = 0;
        switch (key) {
        case SDLK_KP_0: final = 'p'; break;
        case SDLK_KP_1: final = 'q'; break;
        case SDLK_KP_2: final = 'r'; break;
        case SDLK_KP_3: final = 's'; break;
        case SDLK_KP_4: final = 't'; break;
        case SDLK_KP_5: final = 'u'; break;
        case SDLK_KP_6: final = 'v'; break;
        case SDLK_KP_7: final = 'w'; break;
        case SDLK_KP_8: final = 'x'; break;
        case SDLK_KP_9: final = 'y'; break;
        case SDLK_KP_PERIOD: final = 'n'; break;
        case SDLK_KP_PLUS: final = 'l'; break;
        case SDLK_KP_MINUS: final = 'm'; break;
        case SDLK_KP_MULTIPLY: final = 'j'; break;
        case SDLK_KP_DIVIDE: final = 'o'; break;
        case SDLK_KP_ENTER: final = 'M'; break;
        case SDLK_KP_EQUALS: final = 'X'; break;
        default: break;
        }
        if (final) return std::string("\033O") + final;
    }

    // With NumLock off the keypad digits are navigation keys.
    if (!(mod & KMOD_NUM) && !application_keypad) {
        switch (key) {
        case SDLK_KP_0: return EncodeKey(SDLK_INSERT, mod, application_cursor, false);
        case SDLK_KP_1: return EncodeKey(SDLK_END, mod, application_cursor, false);
        case SDLK_KP_2: return EncodeKey(SDLK_DOWN, mod, application_cursor, false);
        case SDLK_KP_3: return EncodeKey(SDLK_PAGEDOWN, mod, application_cursor, false);
        case SDLK_KP_4: return EncodeKey(SDLK_LEFT, mod, application_cursor, false);
        case SDLK_KP_5: return "\033[E";
        case SDLK_KP_6: return EncodeKey(SDLK_RIGHT, mod, application_cursor, false);
        case SDLK_KP_7: return EncodeKey(SDLK_HOME, mod, application_cursor, false);
        case SDLK_KP_8: return EncodeKey(SDLK_UP, mod, application_cursor, false);
        case SDLK_KP_9: return EncodeKey(SDLK_PAGEUP, mod, application_cursor, false);
        case SDLK_KP_PERIOD: return EncodeKey(SDLK_DELETE, mod, application_cursor, false);
        default: break;
        }
    }

    char arrow = 0;
    switch (key) {
    case SDLK_UP: arrow = 'A'; break;
    case SDLK_DOWN: arrow = 'B'; break;
    case SDLK_RIGHT: arrow = 'C'; break;
    case SDLK_LEFT: arrow = 'D'; break;
    case SDLK_HOME: arrow = 'H'; break;
    case SDLK_END: arrow = 'F'; break;
    default: break;
    }
    if (arrow) {
        if (modifier > 1) return "\033[1;" + number(modifier) + arrow;
        return std::string(application_cursor ? "\033O" : "\033[") + arrow;
    }

    int tilde = 0;
    switch (key) {
    case SDLK_INSERT: tilde = 2; break;
    case SDLK_DELETE: tilde = 3; break;
    case SDLK_PAGEUP: tilde = 5; break;
    case SDLK_PAGEDOWN: tilde = 6; break;
    case SDLK_F5: tilde = 15; break;
    case SDLK_F6: tilde = 17; break;
    case SDLK_F7: tilde = 18; break;
    case SDLK_F8: tilde = 19; break;
    case SDLK_F9: tilde = 20; break;
    case SDLK_F10: tilde = 21; break;
    case SDLK_F11: tilde = 23; break;
    case SDLK_F12: tilde = 24; break;
    case SDLK_F13: tilde = 25; break;
    case SDLK_F14: tilde = 26; break;
    case SDLK_F15: tilde = 28; break;
    case SDLK_F16: tilde = 29; break;
    case SDLK_F17: tilde = 31; break;
    case SDLK_F18: tilde = 32; break;
    case SDLK_F19: tilde = 33; break;
    case SDLK_F20: tilde = 34; break;
    default: break;
    }
    if (tilde) return "\033[" + number(tilde) + (modifier > 1 ? ";" + number(modifier) : "") + "~";

    char function = 0;
    switch (key) {
    case SDLK_F1: function = 'P'; break;
    case SDLK_F2: function = 'Q'; break;
    case SDLK_F3: function = 'R'; break;
    case SDLK_F4: function = 'S'; break;
    default: break;
    }
    if (function) return modifier > 1 ? "\033[1;" + number(modifier) + function : std::string("\033O") + function;

    switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return meta + "\r";
    case SDLK_BACKSPACE: return meta + (control ? "\x08" : "\x7f");
    case SDLK_TAB: return shift ? "\033[Z" : meta + "\t";
    case SDLK_ESCAPE: return meta + "\033";
    default: return "";
    }
}
