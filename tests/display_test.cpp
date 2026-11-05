#include "display.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t Bright(const softrender::Image& image, int left, int top, int width, int height) {
    std::size_t result = 0;
    for (int row = top; row < top + height; ++row)
        for (int col = left; col < left + width; ++col) {
            const auto* pixel = image.data() + (row * image.width() + col) * 3;
            if (std::max({pixel[0], pixel[1], pixel[2]}) > 80) ++result;
        }
    return result;
}

std::pair<int, int> InkRows(const softrender::Image& image, int left, int top, int width, int height) {
    int first = height, last = -1;
    for (int row = 0; row < height; ++row) {
        if (Bright(image, left, top + row, width, 1)) {
            first = std::min(first, row);
            last = row;
        }
    }
    return {first, last};
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "font path required");
        Display display(argv[1]);
        Preferences preferences;
        Terminal terminal(display.Rows(preferences), display.Columns(preferences));
        for (bool bitmap : {false, true}) {
            preferences.bitmap = bitmap;
            for (int scale = 1; scale <= 5; ++scale) {
                preferences.scale = scale;
                terminal.Resize(display.Rows(preferences), display.Columns(preferences));
                terminal.Feed("\033[2J\033[HHnhxg");
                display.Paint(terminal, 0, preferences, false);
                const int width = display.CellWidth(preferences), height = display.CellHeight(preferences);
                const auto capital = InkRows(display.image, Display::Margin, Display::Margin, width, height);
                const auto lowercase = InkRows(display.image, Display::Margin + width, Display::Margin, width, height);
                const auto ascender = InkRows(display.image, Display::Margin + width * 2, Display::Margin, width, height);
                const auto descender = InkRows(display.image, Display::Margin + width * 4, Display::Margin, width, height);
                Check(std::abs(capital.second - lowercase.second) <= 1,
                      ("mixed-case baseline at scale " + std::to_string(scale) + (bitmap ? " bitmap" : " stroke") +
                       ": H=" + std::to_string(capital.second) + " n=" + std::to_string(lowercase.second)).c_str());
                Check(std::abs(capital.first - ascender.first) <= (height + 15) / 16, "mixed-case ascender alignment");
                Check(descender.second > lowercase.second, "lowercase descender space");
                terminal.Feed("\033[2J\033[H\033[0mHh");
                display.Paint(terminal, 0, preferences, false);
                const auto regular = display.image;
                const auto regular_pixels = Bright(regular, Display::Margin, Display::Margin, width * 2, height);
                terminal.Feed("\033[2J\033[H\033[1mHh\033[22m");
                display.Paint(terminal, 0, preferences, false);
                Check(Bright(display.image, Display::Margin, Display::Margin, width * 2, height) > regular_pixels, "bold glyph weight");
                terminal.Feed("\033[2J\033[H\033[3mHh\033[23m");
                display.Paint(terminal, 0, preferences, false);
                const auto italic = display.image;
                Check(!(italic == regular), "italic glyph slant");
                terminal.Feed("\033[2J\033[H\033[1;3mHh\033[0m");
                display.Paint(terminal, 0, preferences, false);
                Check(!(display.image == italic), "combined bold italic");
                Check(Bright(display.image, Display::Margin + width * 2, Display::Margin, width, height) == 0, "styled glyph cell clipping");
            }
        }
        preferences = Preferences{};
        terminal.Resize(display.Rows(preferences), display.Columns(preferences));
        for (char32_t point : std::u32string(U"AbZz@#$%&[]{}\\`~^\";\u4e16\u03bb\U0001f600\U0001f680")) {
            Check(display.HasGlyph(point), "missing Unicode glyph");
            terminal.Feed("\033[2J\033[H" + Terminal::Utf8(std::u32string(1, point)));
            display.Paint(terminal, 0, preferences, false);
            Check(Bright(display.image, Display::Margin, Display::Margin, 24, 24) > 3, "blank glyph render");
        }
        terminal.Feed("\033[2J\033[HHello \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x98\x80");
        display.Paint(terminal, 0, preferences, true);
        const auto plain = display.image;
        display.Effects(preferences);
        Check(display.Nonblank() && !(display.image == plain), "CRT effect");
        preferences.amber = true;
        display.Paint(terminal, 0, preferences, true);
        std::uint64_t red = 0, green = 0;
        for (std::size_t index = 0; index < display.image.size(); index += 3) {
            red += display.image.data()[index]; green += display.image.data()[index + 1];
        }
        Check(red > green, "amber palette");
        for (int scale = 1; scale <= 5; ++scale) {
            preferences.scale = scale;
            display.Resize(400, 280);
            terminal.Resize(display.Rows(preferences), display.Columns(preferences));
            display.Paint(terminal, 0, preferences, true);
            Check(display.Columns(preferences) * display.CellWidth(preferences) + 2 * Display::Margin <= 400, "horizontal overflow");
            Check(display.Rows(preferences) * display.CellHeight(preferences) + 2 * Display::Margin <= 280, "vertical overflow");
            display.Settings(preferences);
            display.Effects(preferences);
            Check(display.Nonblank(), "small-window rendering");
        }
        preferences.bitmap = true;
        display.Paint(terminal, 0, preferences, true);
        Check(display.Nonblank(), "bitmap mode");
        std::cout << "ASCII, Unicode, emoji, phosphor, CRT and small-window rendering passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}