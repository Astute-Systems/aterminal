#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

struct Cell {
    std::u32string text = U" ";
    int foreground = -1;
    int background = -1;
    int width = 1;
    bool bold = false;
    bool underline = false;
    bool inverse = false;
    bool italic = false;
    bool conceal = false;
    bool strike = false;
    bool dim = false;
};

class Terminal {
public:
    Terminal(int rows = 24, int columns = 80);
    void Feed(std::string_view bytes);
    void Resize(int rows, int columns);
    const Cell& At(int row, int column) const;
    int Rows() const { return rows_; }
    int Columns() const { return columns_; }
    int Row() const { return row_; }
    int Column() const { return column_; }
    bool CursorVisible() const { return cursor_visible_; }
    bool ApplicationCursor() const { return application_cursor_; }
    bool BracketedPaste() const { return bracketed_paste_; }
    bool Alternate() const { return alternate_; }
    const std::deque<std::vector<Cell>>& History() const { return history_; }
    std::string TakeReply();
    bool TakeBell();
    const std::string& Title() const { return title_; }
    static int Width(char32_t codepoint);
    static std::string Utf8(std::u32string_view text);

private:
    enum class Parse { Ground, Escape, Csi, Osc, OscEscape, Charset, Ignore, IgnoreEscape };
    int rows_, columns_, row_ = 0, column_ = 0;
    int saved_row_ = 0, saved_column_ = 0, main_row_ = 0, main_column_ = 0;
    int top_ = 0, bottom_ = 0, utf_remaining_ = 0;
    char32_t utf_value_ = 0, utf_min_ = 0, repeat_ = 0;
    bool cursor_visible_ = true, application_cursor_ = false, bracketed_paste_ = false;
    bool alternate_ = false, wrap_ = true, pending_wrap_ = false, origin_ = false;
    bool insert_ = false, dec_graphics_ = false, bell_ = false, join_next_ = false;
    bool g1_graphics_ = false, shift_out_ = false;
    bool saved_origin_ = false, saved_wrap_ = true, saved_graphics_ = false, saved_g1_graphics_ = false, saved_shift_out_ = false;
    int charset_slot_ = 0; // 0 G0, 1 G1, 2 a two-byte sequence aterminal ignores
    Parse parse_ = Parse::Ground;
    Cell pen_, saved_pen_;
    std::vector<Cell> cells_, main_cells_;
    std::vector<bool> tabs_;
    std::deque<std::vector<Cell>> history_;
    std::string sequence_, reply_, title_;
    Cell Blank() const;
    void Put(char32_t codepoint);
    void Control(unsigned char byte);
    void Csi(char final);
    void Scroll(int amount);
    void Linefeed();
    void Erase(int begin, int end);
    void Save();
    void Restore();
    void Reset();
    void SoftReset();
    void DefaultTabs(int from);
    void RepairLine(int row);
};