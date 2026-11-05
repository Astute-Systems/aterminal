#include "terminal.h"

#include <algorithm>
#include <utility>

Terminal::Terminal(int rows, int columns)
    : rows_(std::clamp(rows, 1, 500)), columns_(std::clamp(columns, 2, 500)),
      bottom_(rows_ - 1), cells_(rows_ * columns_) { DefaultTabs(0); }

void Terminal::DefaultTabs(int from) {
    tabs_.resize(columns_);
    for (int column = from; column < columns_; ++column) tabs_[column] = column % 8 == 0;
}

const Cell& Terminal::At(int row, int column) const {
    return cells_[std::clamp(row, 0, rows_ - 1) * columns_ + std::clamp(column, 0, columns_ - 1)];
}

Cell Terminal::Blank() const {
    Cell blank;
    blank.background = pen_.background;
    return blank;
}

int Terminal::Width(char32_t point) {
    if ((point >= 0x300 && point <= 0x36f) || (point >= 0x1ab0 && point <= 0x1aff) ||
        (point >= 0x1dc0 && point <= 0x1dff) || (point >= 0x20d0 && point <= 0x20ff) ||
        (point >= 0xfe00 && point <= 0xfe0f) || (point >= 0xfe20 && point <= 0xfe2f) ||
        (point >= 0xe0100 && point <= 0xe01ef) || point == 0x200d ||
        (point >= 0x1f3fb && point <= 0x1f3ff)) return 0;
    if ((point >= 0x1100 && point <= 0x115f) || point == 0x2329 || point == 0x232a ||
        (point >= 0x2e80 && point <= 0xa4cf && point != 0x303f) ||
        (point >= 0xac00 && point <= 0xd7a3) || (point >= 0xf900 && point <= 0xfaff) ||
        (point >= 0xfe10 && point <= 0xfe19) || (point >= 0xfe30 && point <= 0xfe6f) ||
        (point >= 0xff00 && point <= 0xff60) || (point >= 0xffe0 && point <= 0xffe6) ||
        (point >= 0x1f000 && point <= 0x1faff) || (point >= 0x20000 && point <= 0x3ffff)) return 2;
    return 1;
}

std::string Terminal::Utf8(std::u32string_view text) {
    std::string result;
    for (char32_t point : text) {
        if (point < 0x80) result += static_cast<char>(point);
        else if (point < 0x800) {
            result += static_cast<char>(0xc0 | (point >> 6));
            result += static_cast<char>(0x80 | (point & 63));
        } else if (point < 0x10000) {
            result += static_cast<char>(0xe0 | (point >> 12));
            result += static_cast<char>(0x80 | ((point >> 6) & 63));
            result += static_cast<char>(0x80 | (point & 63));
        } else {
            result += static_cast<char>(0xf0 | (point >> 18));
            result += static_cast<char>(0x80 | ((point >> 12) & 63));
            result += static_cast<char>(0x80 | ((point >> 6) & 63));
            result += static_cast<char>(0x80 | (point & 63));
        }
    }
    return result;
}

void Terminal::RepairLine(int row) {
    for (int col = 0; col < columns_; ++col) {
        Cell& cell = cells_[row * columns_ + col];
        if (cell.width == 0 && (col == 0 || cells_[row * columns_ + col - 1].width != 2)) cell = Blank();
        if (cell.width == 2 && (col + 1 == columns_ || cells_[row * columns_ + col + 1].width != 0)) cell = Blank();
    }
}

void Terminal::Put(char32_t point) {
    if (Width(point) > 0) repeat_ = point;
    if ((shift_out_ ? g1_graphics_ : dec_graphics_) && point >= U'_' && point <= U'~') {
        static constexpr char32_t graphics[] = U" \u25c6\u2592\u2409\u240c\u240d\u240a\u00b0\u00b1\u2424\u240b\u2518\u2510\u250c\u2514\u253c\u23ba\u23bb\u2500\u23bc\u23bd\u251c\u2524\u2534\u252c\u2502\u2264\u2265\u03c0\u2260\u00a3\u00b7";
        point = graphics[point - U'_'];
    }
    const int width = Width(point);
    if ((width == 0 || join_next_) && (column_ > 0 || pending_wrap_)) {
        int previous = row_ * columns_ + (pending_wrap_ ? column_ : column_ - 1);
        if (cells_[previous].width == 0 && previous > 0) --previous;
        if (cells_[previous].text.size() < 32) cells_[previous].text += point;
        join_next_ = point == 0x200d;
        return;
    }
    join_next_ = false;
    if (pending_wrap_ || (width == 2 && column_ == columns_ - 1)) {
        if (wrap_) { column_ = 0; Linefeed(); }
        pending_wrap_ = false;
    }
    if (width == 2 && column_ == columns_ - 1) return;
    if (insert_) {
        auto begin = cells_.begin() + row_ * columns_;
        std::move_backward(begin + column_, begin + columns_ - std::max(width, 1), begin + columns_);
    }
    if (cells_[row_ * columns_ + column_].width == 0 && column_ > 0)
        cells_[row_ * columns_ + column_ - 1] = Blank();
    Cell cell = pen_;
    cell.text.assign(1, point);
    cell.width = std::max(width, 1);
    cells_[row_ * columns_ + column_] = cell;
    if (width == 2) {
        cell.text.clear();
        cell.width = 0;
        cells_[row_ * columns_ + column_ + 1] = cell;
    }
    RepairLine(row_);
    column_ += std::max(width, 1);
    if (column_ >= columns_) { column_ = columns_ - 1; pending_wrap_ = wrap_; }
}

void Terminal::Scroll(int amount) {
    amount = std::clamp(amount, -(bottom_ - top_ + 1), bottom_ - top_ + 1);
    if (amount > 0) {
        if (!alternate_ && top_ == 0 && bottom_ == rows_ - 1) {
            for (int line = 0; line < amount; ++line) {
                history_.emplace_back(cells_.begin() + line * columns_, cells_.begin() + (line + 1) * columns_);
                if (history_.size() > 10000) history_.pop_front();
            }
        }
        std::move(cells_.begin() + (top_ + amount) * columns_, cells_.begin() + (bottom_ + 1) * columns_, cells_.begin() + top_ * columns_);
        Erase((bottom_ + 1 - amount) * columns_, (bottom_ + 1) * columns_);
    } else if (amount < 0) {
        std::move_backward(cells_.begin() + top_ * columns_, cells_.begin() + (bottom_ + 1 + amount) * columns_, cells_.begin() + (bottom_ + 1) * columns_);
        Erase(top_ * columns_, (top_ - amount) * columns_);
    }
}

void Terminal::Linefeed() {
    pending_wrap_ = false;
    if (row_ == bottom_) Scroll(1);
    else row_ = std::min(row_ + 1, rows_ - 1);
}

void Terminal::Erase(int begin, int end) {
    begin = std::clamp(begin, 0, rows_ * columns_);
    end = std::clamp(end, begin, rows_ * columns_);
    std::fill(cells_.begin() + begin, cells_.begin() + end, Blank());
    if (begin < end) {
        RepairLine(begin / columns_);
        RepairLine((end - 1) / columns_);
    }
}

void Terminal::Save() {
    saved_row_ = row_; saved_column_ = column_; saved_pen_ = pen_;
    saved_origin_ = origin_; saved_wrap_ = wrap_;
    saved_graphics_ = dec_graphics_; saved_g1_graphics_ = g1_graphics_; saved_shift_out_ = shift_out_;
}
void Terminal::Restore() {
    row_ = std::clamp(saved_row_, 0, rows_ - 1);
    column_ = std::clamp(saved_column_, 0, columns_ - 1);
    pen_ = saved_pen_;
    origin_ = saved_origin_; wrap_ = saved_wrap_;
    dec_graphics_ = saved_graphics_; g1_graphics_ = saved_g1_graphics_; shift_out_ = saved_shift_out_;
    pending_wrap_ = false;
}

void Terminal::SoftReset() {
    pen_ = saved_pen_ = Cell{};
    saved_row_ = saved_column_ = top_ = 0;
    saved_origin_ = saved_graphics_ = saved_g1_graphics_ = saved_shift_out_ = false;
    saved_wrap_ = true;
    bottom_ = rows_ - 1;
    origin_ = insert_ = dec_graphics_ = g1_graphics_ = shift_out_ = pending_wrap_ = false;
    repeat_ = 0;
    wrap_ = cursor_visible_ = true;
    application_cursor_ = false;
}

void Terminal::Reset() {
    SoftReset();
    cells_.assign(rows_ * columns_, Cell{});
    row_ = column_ = 0;
    bracketed_paste_ = false;
    DefaultTabs(0);
}

void Terminal::Control(unsigned char byte) {
    if (byte == '\r') { column_ = 0; pending_wrap_ = false; }
    else if (byte == '\n' || byte == '\v' || byte == '\f') Linefeed();
    else if (byte == '\b') { column_ = std::max(0, column_ - 1); pending_wrap_ = false; }
    else if (byte == '\t') {
        do ++column_; while (column_ < columns_ - 1 && !tabs_[column_]);
        column_ = std::min(column_, columns_ - 1);
        pending_wrap_ = false;
    }
    else if (byte == 7) bell_ = true;
    else if (byte == 0x0e) shift_out_ = true;
    else if (byte == 0x0f) shift_out_ = false;
}

void Terminal::Csi(char final) {
    bool private_mode = !sequence_.empty() && sequence_[0] == '?';
    // Sequences that share a final byte with a simpler one, such as ESC[>4;2m
    // (not SGR) and ESC[1 @ (not ICH), must not fall through to it.
    const char prefix = sequence_.empty() ? '\0' : sequence_[0];
    const bool intermediate = sequence_.find_first_of(" !\"#$%&'()*+,-./") != std::string::npos;
    if (final == 'p' && sequence_ == "!") { SoftReset(); return; }
    if (final == 'c' && prefix == '>') { reply_ += "\033[>0;0;0c"; return; }
    if (intermediate || prefix == '>' || prefix == '=' || prefix == '<') return;
    std::vector<int> args(1, 0);
    for (unsigned char byte : sequence_) {
        if (byte >= '0' && byte <= '9') args.back() = std::min(100000, args.back() * 10 + byte - '0');
        else if ((byte == ';' || byte == ':') && args.size() < 64) args.push_back(0);
    }
    const auto arg = [&](std::size_t index, int fallback = 1) {
        return index < args.size() && args[index] != 0 ? args[index] : fallback;
    };
    const int count = std::min(arg(0), std::max(rows_, columns_));
    const int first = origin_ ? top_ : 0;
    const int last = origin_ ? bottom_ : rows_ - 1;
    // Relative moves stop at a margin when they start inside it, in either origin mode.
    const int above = row_ >= top_ ? top_ : 0;
    const int below = row_ <= bottom_ ? bottom_ : rows_ - 1;
    if (final != 'm' && final != 'b') pending_wrap_ = false;
    switch (final) {
    case 'A': row_ = std::max(above, row_ - count); break;
    case 'B': case 'e': row_ = std::min(below, row_ + count); break;
    case 'C': case 'a': column_ = std::min(columns_ - 1, column_ + count); break;
    case 'D': column_ = std::max(0, column_ - count); break;
    case 'E': row_ = std::min(below, row_ + count); column_ = 0; break;
    case 'F': row_ = std::max(above, row_ - count); column_ = 0; break;
    case 'G': case '`': column_ = std::clamp(arg(0) - 1, 0, columns_ - 1); break;
    case 'd': row_ = std::clamp(first + arg(0) - 1, first, last); break;
    case 'b':
        if (repeat_) for (int repeat = 0; repeat < count; ++repeat) Put(repeat_);
        break;
    case 'Z':
        for (int tab = 0; tab < count; ++tab) {
            if (column_ > 0) --column_;
            while (column_ > 0 && !tabs_[column_]) --column_;
        }
        break;
    case 'g':
        if (args[0] == 0) tabs_[column_] = false;
        else if (args[0] == 3) tabs_.assign(columns_, false);
        break;
    case 'H': case 'f':
        row_ = std::clamp(first + arg(0) - 1, first, last);
        column_ = std::clamp(arg(1) - 1, 0, columns_ - 1); break;
    case 'J':
        if (args[0] == 0) Erase(row_ * columns_ + column_, rows_ * columns_);
        else if (args[0] == 1) Erase(0, row_ * columns_ + column_ + 1);
        else if (args[0] == 2) Erase(0, rows_ * columns_);
        else if (args[0] == 3) history_.clear();
        break;
    case 'K':
        if (args[0] == 0) Erase(row_ * columns_ + column_, (row_ + 1) * columns_);
        else if (args[0] == 1) Erase(row_ * columns_, row_ * columns_ + column_ + 1);
        else if (args[0] == 2) Erase(row_ * columns_, (row_ + 1) * columns_);
        break;
    case 'X': Erase(row_ * columns_ + column_, row_ * columns_ + std::min(columns_, column_ + count)); break;
    case 'P': case '@': {
        auto begin = cells_.begin() + row_ * columns_;
        const int length = std::min(count, columns_ - column_);
        if (final == 'P') {
            std::move(begin + column_ + length, begin + columns_, begin + column_);
            std::fill(begin + columns_ - length, begin + columns_, Blank());
        } else {
            std::move_backward(begin + column_, begin + columns_ - length, begin + columns_);
            std::fill(begin + column_, begin + column_ + length, Blank());
        }
        RepairLine(row_); break;
    }
    case 'L': case 'M': {
        if (row_ < top_ || row_ > bottom_) break;
        const int previous = top_;
        top_ = row_;
        Scroll(final == 'M' ? count : -count);
        top_ = previous; break;
    }
    case 'S': Scroll(count); break;
    case 'T': Scroll(-count); break;
    case 'r':
        if (arg(0) < arg(1, rows_)) {
            top_ = std::clamp(arg(0) - 1, 0, rows_ - 1);
            bottom_ = std::clamp(arg(1, rows_) - 1, top_, rows_ - 1);
            row_ = origin_ ? top_ : 0; column_ = 0;
        } break;
    case 's': Save(); break;
    case 'u': Restore(); break;
    case 'n':
        if (args[0] == 5) reply_ += "\033[0n";
        if (args[0] == 6) reply_ += "\033[" + std::to_string(row_ - first + 1) + ";" + std::to_string(column_ + 1) + "R";
        break;
    case 'c': reply_ += "\033[?1;2c"; break;
    case 'h': case 'l': {
        const bool enabled = final == 'h';
        for (int mode : args) {
            if (!private_mode) { if (mode == 4) insert_ = enabled; continue; }
            if (mode == 1) application_cursor_ = enabled;
            else if (mode == 6) { origin_ = enabled; row_ = origin_ ? top_ : 0; column_ = 0; }
            else if (mode == 7) wrap_ = enabled;
            else if (mode == 25) cursor_visible_ = enabled;
            else if (mode == 2004) bracketed_paste_ = enabled;
            else if ((mode == 1049 || mode == 47 || mode == 1047) && alternate_ != enabled) {
                if (enabled) {
                    main_cells_ = std::move(cells_);
                    main_row_ = row_; main_column_ = column_;
                    cells_.assign(rows_ * columns_, Cell{});
                    row_ = column_ = 0;
                } else {
                    cells_ = std::move(main_cells_);
                    row_ = std::clamp(main_row_, 0, rows_ - 1);
                    column_ = std::clamp(main_column_, 0, columns_ - 1);
                }
                alternate_ = enabled;
                top_ = 0; bottom_ = rows_ - 1;
            }
        } break;
    }
    case 'm':
        for (std::size_t index = 0; index < args.size(); ++index) {
            const int value = args[index];
            if (value == 0) pen_ = Cell{};
            else if (value == 1) pen_.bold = true;
            else if (value == 2) pen_.dim = true;
            else if (value == 3) pen_.italic = true;
            else if (value == 4) pen_.underline = true;
            else if (value == 7) pen_.inverse = true;
            else if (value == 8) pen_.conceal = true;
            else if (value == 9) pen_.strike = true;
            else if (value == 22) pen_.bold = pen_.dim = false;
            else if (value == 23) pen_.italic = false;
            else if (value == 24) pen_.underline = false;
            else if (value == 27) pen_.inverse = false;
            else if (value == 28) pen_.conceal = false;
            else if (value == 29) pen_.strike = false;
            else if (value >= 30 && value <= 37) pen_.foreground = value - 30;
            else if (value >= 40 && value <= 47) pen_.background = value - 40;
            else if (value >= 90 && value <= 97) pen_.foreground = value - 90 + 8;
            else if (value >= 100 && value <= 107) pen_.background = value - 100 + 8;
            else if (value == 39) pen_.foreground = -1;
            else if (value == 49) pen_.background = -1;
            else if ((value == 38 || value == 48) && index + 2 < args.size()) {
                int color = -1;
                if (args[index + 1] == 5) { color = std::clamp(args[index + 2], 0, 255); index += 2; }
                else if (args[index + 1] == 2 && index + 4 < args.size()) {
                    color = 0x1000000 | (std::clamp(args[index + 2], 0, 255) << 16) |
                            (std::clamp(args[index + 3], 0, 255) << 8) | std::clamp(args[index + 4], 0, 255);
                    index += 4;
                }
                if (value == 38) pen_.foreground = color; else pen_.background = color;
            }
        } break;
    default: break;
    }
}

void Terminal::Feed(std::string_view bytes) {
    for (unsigned char byte : bytes) {
        if (parse_ == Parse::Osc || parse_ == Parse::OscEscape) {
            if (byte == 7 || (parse_ == Parse::OscEscape && byte == '\\')) {
                if (sequence_.rfind("0;", 0) == 0 || sequence_.rfind("2;", 0) == 0) title_ = sequence_.substr(2);
                parse_ = Parse::Ground;
            } else if (byte == 27) parse_ = Parse::OscEscape;
            else { if (sequence_.size() < 4096) sequence_ += static_cast<char>(byte); parse_ = Parse::Osc; }
            continue;
        }
        if (parse_ == Parse::Ignore || parse_ == Parse::IgnoreEscape) {
            if (parse_ == Parse::IgnoreEscape && byte == '\\') parse_ = Parse::Ground;
            else parse_ = byte == 27 ? Parse::IgnoreEscape : Parse::Ignore;
            continue;
        }
        if (byte == 27) { parse_ = Parse::Escape; utf_remaining_ = 0; continue; }
        if (byte < 32) { Control(byte); continue; }
        if (parse_ == Parse::Charset) {
            if (charset_slot_ == 0) dec_graphics_ = byte == '0';
            else if (charset_slot_ == 1) g1_graphics_ = byte == '0';
            parse_ = Parse::Ground;
            continue;
        }
        if (parse_ == Parse::Escape) {
            parse_ = Parse::Ground;
            if (byte == '[') { parse_ = Parse::Csi; sequence_.clear(); }
            else if (byte == ']') { parse_ = Parse::Osc; sequence_.clear(); }
            else if (byte == '(' || byte == ')') { charset_slot_ = byte == ')'; parse_ = Parse::Charset; }
            else if (byte == '*' || byte == '+' || byte == '-' || byte == '.' || byte == '/' ||
                     byte == '#' || byte == '%' || byte == ' ') { charset_slot_ = 2; parse_ = Parse::Charset; }
            else if (byte == 'H') tabs_[column_] = true;
            else if (byte == 'P' || byte == '^' || byte == '_') parse_ = Parse::Ignore;
            else if (byte == '7') Save();
            else if (byte == '8') Restore();
            else if (byte == 'D') Linefeed();
            else if (byte == 'E') { column_ = 0; Linefeed(); }
            else if (byte == 'M') { if (row_ == top_) Scroll(-1); else row_ = std::max(0, row_ - 1); }
            else if (byte == 'c') Reset();
            else if (byte == 'Z') reply_ += "\033[?1;2c";
            continue;
        }
        if (parse_ == Parse::Csi) {
            if (byte >= 0x40 && byte <= 0x7e) { Csi(static_cast<char>(byte)); parse_ = Parse::Ground; }
            else if (sequence_.size() < 512) sequence_ += static_cast<char>(byte);
            else parse_ = Parse::Ground;
            continue;
        }
        if (byte == 127) continue;
        if (utf_remaining_) {
            if ((byte & 0xc0) == 0x80) {
                utf_value_ = (utf_value_ << 6) | (byte & 63);
                if (--utf_remaining_ == 0)
                    Put(utf_value_ >= utf_min_ && utf_value_ <= 0x10ffff && !(utf_value_ >= 0xd800 && utf_value_ <= 0xdfff) ? utf_value_ : 0xfffd);
                continue;
            }
            utf_remaining_ = 0;
            Put(0xfffd);
        }
        if (byte < 128) Put(byte);
        else if (byte >= 0xc2 && byte <= 0xdf) { utf_value_ = byte & 31; utf_remaining_ = 1; utf_min_ = 0x80; }
        else if (byte >= 0xe0 && byte <= 0xef) { utf_value_ = byte & 15; utf_remaining_ = 2; utf_min_ = 0x800; }
        else if (byte >= 0xf0 && byte <= 0xf4) { utf_value_ = byte & 7; utf_remaining_ = 3; utf_min_ = 0x10000; }
        else Put(0xfffd);
    }
}

void Terminal::Resize(int rows, int columns) {
    rows = std::clamp(rows, 1, 500); columns = std::clamp(columns, 2, 500);
    const auto resize = [&](std::vector<Cell>& grid) {
        if (grid.empty()) return;
        std::vector<Cell> next(rows * columns);
        for (int row = 0; row < std::min(rows, rows_); ++row)
            std::copy_n(grid.begin() + row * columns_, std::min(columns, columns_), next.begin() + row * columns);
        grid = std::move(next);
    };
    resize(cells_); resize(main_cells_);
    const int previous_columns = columns_;
    rows_ = rows; columns_ = columns;
    if (columns > previous_columns) DefaultTabs(previous_columns); else tabs_.resize(columns);
    row_ = std::min(row_, rows - 1); column_ = std::min(column_, columns - 1);
    top_ = 0; bottom_ = rows - 1; pending_wrap_ = false;
    for (int row = 0; row < rows_; ++row) RepairLine(row);
}

std::string Terminal::TakeReply() { return std::exchange(reply_, {}); }
bool Terminal::TakeBell() { return std::exchange(bell_, false); }