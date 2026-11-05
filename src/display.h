#pragma once

#include "terminal.h"
#include "softrender/types.h"
#include <array>
#include <string>
#include <unordered_map>

struct Preferences {
    bool amber = false;
    bool sound = true;
    bool crt = true;
    bool bitmap = false;
    int scale = 6;
};

class Display {
public:
    explicit Display(const std::string& font_path);
    softrender::Image image{1024, 720};
    void Resize(int width, int height);
    static int CellWidth(const Preferences& preferences) { return preferences.scale * 6; }
    static int CellHeight(const Preferences& preferences) { return preferences.scale * 12; }
    int Rows(const Preferences& preferences) const;
    int Columns(const Preferences& preferences) const;
    void Paint(const Terminal& terminal, int scrollback, const Preferences& preferences, bool cursor,
               int selection_begin = -1, int selection_end = -1);
    void Settings(const Preferences& preferences);
    void Effects(const Preferences& preferences);
    bool HasGlyph(char32_t point) const { return glyphs_.find(point) != glyphs_.end(); }
    bool Nonblank() const;
    static constexpr int Margin = 24;
    static constexpr int MaxScale = 6;

private:
    struct Glyph { int width = 8; std::array<std::uint32_t, 16> rows{}; };
    std::unordered_map<char32_t, Glyph> glyphs_;
    std::vector<float> shade_;
    void GlyphAt(softrender::Image& target, char32_t point, int left, int top, int width, int height,
                 softrender::Rgb color, bool bitmap, bool bold = false, bool italic = false);
    void Rect(int left, int top, int width, int height, softrender::Rgb color);
};