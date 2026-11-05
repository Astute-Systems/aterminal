#include "display.h"

#include "softrender/parallel.h"
#include "softrender/post.h"
#include "softrender/text.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

using softrender::Rgb;

namespace {
Rgb Phosphor(const Preferences& preferences) { return preferences.amber ? Rgb{255, 183, 65} : Rgb{85, 255, 144}; }

// Line directions for the light box-drawing characters, or 0 for anything else.
enum : int { kLeft = 1, kRight = 2, kUp = 4, kDown = 8 };
int BoxLines(char32_t point) {
    switch (point) {
    case 0x2500: return kLeft | kRight;
    case 0x2502: return kUp | kDown;
    case 0x250c: return kRight | kDown;
    case 0x2510: return kLeft | kDown;
    case 0x2514: return kRight | kUp;
    case 0x2518: return kLeft | kUp;
    case 0x251c: return kRight | kUp | kDown;
    case 0x2524: return kLeft | kUp | kDown;
    case 0x252c: return kLeft | kRight | kDown;
    case 0x2534: return kLeft | kRight | kUp;
    case 0x253c: return kLeft | kRight | kUp | kDown;
    default: return 0;
    }
}

float Luminance(int color, bool bold) {
    if (color < 0) return bold ? 1.0f : 0.84f;
    if (color >= 0x1000000) {
        return 0.4f + 0.6f * (((color >> 16) & 255) * 0.299f + ((color >> 8) & 255) * 0.587f + (color & 255) * 0.114f) / 255.0f;
    }
    if (color < 16) return (color == 0 ? 0.25f : 0.65f) + ((bold || color >= 8) ? 0.25f : 0.0f);
    if (color >= 232) return 0.4f + (color - 232) * 0.6f / 23.0f;
    const int cube = color - 16;
    return 0.45f + 0.55f * ((cube / 36) * 0.299f + ((cube / 6) % 6) * 0.587f + (cube % 6) * 0.114f) / 5.0f;
}
}

Display::Display(const std::string& font_path) {
    std::ifstream file(font_path);
    if (!file) throw std::runtime_error("Unicode bitmap font not found: " + font_path);
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto count = line.size() - colon - 1;
        if (count != 32 && count != 64 && count != 128) continue;
        Glyph glyph;
        glyph.width = static_cast<int>(count / 4);
        const int digits = glyph.width / 4;
        for (int row = 0; row < 16; ++row)
            glyph.rows[row] = static_cast<std::uint32_t>(std::stoul(line.substr(colon + 1 + row * digits, digits), nullptr, 16));
        glyphs_.emplace(static_cast<char32_t>(std::stoul(line.substr(0, colon), nullptr, 16)), glyph);
    }
    if (glyphs_.empty()) throw std::runtime_error("Unicode bitmap font is empty or invalid");
    Resize(1024, 720);
}

void Display::Resize(int width, int height) {
    width = std::clamp(width, 160, 4096);
    height = std::clamp(height, 120, 2160);
    image = softrender::Image(width, height);
    shade_.resize(static_cast<std::size_t>(width) * height);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const float dx = (column - width / 2.0f) / (width / 2.0f);
            const float dy = (row - height / 2.0f) / (height / 2.0f);
            const float distance = dx * dx + dy * dy;
            shade_[static_cast<std::size_t>(row) * width + column] = std::max(0.3f, 1.0f - 0.2f * distance * distance - 0.02f * distance);
        }
    }
}

int Display::Rows(const Preferences& preferences) const {
    return std::clamp((static_cast<int>(image.height()) - Margin * 2) / CellHeight(preferences), 1, 500);
}
int Display::Columns(const Preferences& preferences) const {
    return std::clamp((static_cast<int>(image.width()) - Margin * 2) / CellWidth(preferences), 2, 500);
}

void Display::Rect(int left, int top, int width, int height, Rgb color) {
    for (int row = std::max(0, top); row < std::min(static_cast<int>(image.height()), top + height); ++row)
        for (int col = std::max(0, left); col < std::min(static_cast<int>(image.width()), left + width); ++col)
            image.Set(col, row, color);
}

void Display::GlyphAt(softrender::Image& target, char32_t point, int left, int top, int width, int height,
                      Rgb color, bool bitmap, bool bold, bool italic) {
    if (point == U' ' || point == 0x200d || (point >= 0xfe00 && point <= 0xfe0f)) return;
    if (bold || italic) {
        softrender::Image mask(width, height);
        GlyphAt(mask, point, 0, 0, width, height, {255, 255, 255}, bitmap);
        const int weight = bold ? std::max(1, width / 16) : 0;
        const float slant = italic ? width / 6.0f : 0.0f;
        const float content_width = std::max(1.0f, width - std::ceil(slant) - weight);
        const float baseline = std::max(1, (height * 14 + 15) / 16 - 1);
        const auto coverage = [&](float column, int row) {
            if (column < 0 || column > width - 1) return 0.0f;
            const int first = static_cast<int>(column), next = std::min(first + 1, width - 1);
            const float fraction = column - first;
            const auto offset = static_cast<std::size_t>(row) * width;
            return (mask.data()[(offset + first) * 3] * (1.0f - fraction) +
                    mask.data()[(offset + next) * 3] * fraction) / 255.0f;
        };
        for (int row = 0; row < height; ++row) {
            const float shift = std::max(0.0f, (baseline - row) / baseline) * slant;
            for (int column = 0; column < width; ++column) {
                float alpha = 0;
                for (int offset = 0; offset <= weight; ++offset)
                    alpha = std::max(alpha, coverage((column - shift - offset) * width / content_width, row));
                const int px = left + column, py = top + row;
                if (alpha <= 0 || px < 0 || py < 0 || px >= static_cast<int>(target.width()) || py >= static_cast<int>(target.height())) continue;
                const auto* pixel = target.data() + (static_cast<std::size_t>(py) * target.width() + px) * 3;
                target.Set(px, py, softrender::Mix({pixel[0], pixel[1], pixel[2]}, color, alpha));
            }
        }
        return;
    }
    static const std::string strokes = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:!()/%+?=><'-_";
    const int cap_top = (height * 4 + 15) / 16;
    const int baseline = (height * 14 + 15) / 16;
    const float stroke_scale = std::max(1.0f, (baseline - cap_top) / 6.7f);
    const int stroke_top = top + cap_top - static_cast<int>(std::lround(0.15f * stroke_scale));
    if (!bitmap && point < 128 && strokes.find(static_cast<char>(point)) != std::string::npos) {
        softrender::DrawText(target, std::string(1, static_cast<char>(point)), left, stroke_top,
                            stroke_scale, color);
        return;
    }
    const auto found = glyphs_.find(point);
    if (found == glyphs_.end()) {
        softrender::DrawText(target, "?", left, stroke_top, stroke_scale, color);
        return;
    }
    const Glyph& glyph = found->second;
    for (int row = 0; row < height; ++row) {
        const int source_row = row * 16 / height;
        for (int col = 0; col < width; ++col) {
            const int source_col = col * glyph.width / width;
            if ((glyph.rows[source_row] >> (glyph.width - 1 - source_col)) & 1u)
                target.Set(left + col, top + row, color);
        }
    }
}

void Display::Paint(const Terminal& terminal, int scrollback, const Preferences& preferences, bool cursor,
                    int selection_begin, int selection_end) {
    const Rgb phosphor = Phosphor(preferences);
    const Rgb rest = preferences.amber ? Rgb{13, 8, 2} : Rgb{2, 12, 6};
    image.Clear(rest);
    const int cell_width = CellWidth(preferences), cell_height = CellHeight(preferences);
    const int history_size = static_cast<int>(terminal.History().size());
    scrollback = std::clamp(scrollback, 0, history_size);
    if (selection_begin > selection_end) std::swap(selection_begin, selection_end);
    for (int row = 0; row < terminal.Rows(); ++row) {
        const int source = history_size - scrollback + row;
        for (int col = 0; col < terminal.Columns(); ++col) {
            Cell blank;
            const Cell* cell = &blank;
            if (source < history_size) {
                const auto& line = terminal.History()[source];
                if (col < static_cast<int>(line.size())) cell = &line[col];
            } else if (source - history_size < terminal.Rows()) cell = &terminal.At(source - history_size, col);
            if (cell->width == 0) continue;
            const int left = Margin + col * cell_width, top = Margin + row * cell_height;
            const int span = std::min(cell->width, terminal.Columns() - col);
            const int index = row * terminal.Columns() + col;
            const bool selected = selection_begin >= 0 && index >= selection_begin && index <= selection_end;
            // With one phosphor colour, black text on a coloured background is reverse video.
            const bool inverse = (cell->inverse || (cell->foreground == 0 && cell->background > 0)) != selected;
            const Rgb foreground = inverse ? rest : softrender::Scale(phosphor, Luminance(cell->foreground, cell->bold) * (cell->dim ? 0.6f : 1.0f));
            Rgb background = cell->background <= 0 ? rest : softrender::Scale(phosphor, Luminance(cell->background, false) * 0.25f);
            if (inverse) background = softrender::Scale(phosphor, 0.8f);
            Rect(left, top, cell_width * span, cell_height, background);
            if (!cell->conceal) {
                bool base = true;
                for (char32_t point : cell->text) {
                    if (!base && Terminal::Width(point) > 0) continue;
                    if (const int lines = base ? BoxLines(point) : 0) {
                        // Drawn to the cell edges, unlike a font glyph, so neighbours join up.
                        const int thick = preferences.scale * (cell->bold ? 2 : 1);
                        const int x = left + (cell_width - thick) / 2, y = top + (cell_height - thick) / 2;
                        if (lines & kLeft) Rect(left, y, x + thick - left, thick, foreground);
                        if (lines & kRight) Rect(x, y, left + cell_width - x, thick, foreground);
                        if (lines & kUp) Rect(x, top, thick, y + thick - top, foreground);
                        if (lines & kDown) Rect(x, y, thick, top + cell_height - y, foreground);
                    } else {
                        // Other box and block characters also fill the cell so they tile.
                        const int inset = point >= 0x2500 && point <= 0x259f ? 0 : 1;
                        GlyphAt(image, point, left, top + inset, cell_width * span, cell_height - inset * 2,
                            foreground, preferences.bitmap, cell->bold, cell->italic);
                    }
                    base = false;
                }
            }
            if (cell->underline) Rect(left, top + cell_height - 2, cell_width * span, 1, foreground);
            if (cell->strike) Rect(left, top + cell_height / 2, cell_width * span, 1, foreground);
        }
    }
    if (!scrollback && cursor && terminal.CursorVisible()) {
        const int left = Margin + terminal.Column() * cell_width;
        const int top = Margin + terminal.Row() * cell_height;
        for (int row = 0; row < cell_height; ++row)
            for (int col = 0; col < cell_width; ++col) {
                const int px = left + col, py = top + row;
                if (px >= static_cast<int>(image.width()) || py >= static_cast<int>(image.height())) continue;
                auto* pixel = image.data() + (static_cast<std::size_t>(py) * image.width() + px) * 3;
                image.Set(px, py, {static_cast<std::uint8_t>(phosphor.r - std::min(phosphor.r, pixel[0])),
                                  static_cast<std::uint8_t>(phosphor.g - std::min(phosphor.g, pixel[1])),
                                  static_cast<std::uint8_t>(phosphor.b - std::min(phosphor.b, pixel[2]))});
            }
    }
}

void Display::Settings(const Preferences& preferences) {
    const int left = std::max(8, (static_cast<int>(image.width()) - 336) / 2);
    const int top = std::max(8, (static_cast<int>(image.height()) - 264) / 2);
    const Rgb ink = Phosphor(preferences);
    Rect(left - 2, top - 2, 340, 268, softrender::Scale(ink, 0.4f));
    Rect(left, top, 336, 264, {6, 9, 7});
    softrender::DrawText(image, "TERMINAL SETUP", left + 24, top + 22, 2.0f, ink);
    const auto label = [&](const std::string& text, int row, bool active) {
        const int ypos = top + 65 + row * 40;
        Rect(left + 24, ypos, 16, 16, active ? ink : softrender::Scale(ink, 0.15f));
        softrender::DrawText(image, text, left + 56, ypos + 1, 1.7f, ink);
    };
    label("GREEN PHOSPHOR", 0, !preferences.amber);
    label("AMBER PHOSPHOR", 1, preferences.amber);
    label("ADLIB KEY SOUND", 2, preferences.sound);
    label("CRT EFFECTS", 3, preferences.crt);
    label("BITMAP FONT", 4, preferences.bitmap);
}

void Display::Effects(const Preferences& preferences) {
    if (!preferences.crt) return;
    softrender::Bloom(image, 0.6f, 100.0f, 4);
    const int width = static_cast<int>(image.width()), height = static_cast<int>(image.height());
    softrender::ParallelRows(0, height, [&](int first, int last) {
        for (int row = first; row < last; ++row) {
            const float scanline = row % 3 == 2 ? 0.72f : 1.0f;
            for (int col = 0; col < width; ++col) {
                const auto index = static_cast<std::size_t>(row) * width + col;
                const float shade = shade_[index] * scanline;
                auto* pixel = image.data() + index * 3;
                for (int channel = 0; channel < 3; ++channel) pixel[channel] = static_cast<std::uint8_t>(pixel[channel] * shade);
            }
        }
    });
}

bool Display::Nonblank() const {
    std::size_t bright = 0;
    for (std::size_t index = 0; index < image.size(); index += 3)
        if (std::max({image.data()[index], image.data()[index + 1], image.data()[index + 2]}) > 80) ++bright;
    return bright > 100;
}