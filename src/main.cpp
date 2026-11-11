#include "display.h"
#include "motd.h"
#include "prompt.h"
#include "session.h"
#include "coriolis/adlib_synth.h"
#include "softrender/png.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

constexpr std::size_t kMaxStartupCommand = 60000; // Must fit a pipe buffer; see Session
constexpr int kMaxConfigFiles = 8;
constexpr char kDefaultPrompt[] = "redhat";

enum class Bell { Visual, Audible, None };

struct Options {
    std::string shell = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/sh";
    std::string command, startup, screenshot, motd;
    int frames = 0;
    int width = 0, height = 0; // 0 sizes the window to the default console
    bool self_test = false, fullscreen = false, maximized = false, hold = false, no_blink = false;
    std::string title = "aterminal";
    int columns = 80, rows = 25, console = 1, scrollback = 10000;
    Bell bell = Bell::Visual;
    ShellOptions shell_options;
};

// Reads one option per line, as `name value`, `name=value` or `name`, without the
// leading dashes. Blank lines and lines starting with # are ignored.
std::vector<std::string> ConfigArguments(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read config file: " + path);
    std::vector<std::string> arguments;
    std::string line;
    while (std::getline(file, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        const auto last = line.find_last_not_of(" \t\r");
        std::string option = line.substr(first, last - first + 1);
        const auto separator = option.find_first_of(" \t=");
        if (separator != std::string::npos && option[separator] != '=') {
            const auto value = option.find_first_not_of(" \t", separator);
            option = option.substr(0, separator) + "=" + option.substr(value);
        }
        arguments.push_back("--" + option);
    }
    return arguments;
}

int IntegerFlag(const std::string& value, int minimum, int maximum, const std::string& flag) {
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result < minimum || result > maximum)
        throw std::runtime_error(flag + " must be an integer from " + std::to_string(minimum) + " to " + std::to_string(maximum));
    return result;
}

class Audio {
public:
    Audio() {
        coriolis::AdLibSynth synth(7, 44100);
        samples_ = synth.Render(coriolis::SoundEffect::kUiMove);
        samples_.resize(std::min<std::size_t>(samples_.size(), 1102));
        for (std::size_t index = 0; index < samples_.size(); ++index) {
            const float fade = std::min(1.0f, static_cast<float>(samples_.size() - index) / 150.0f);
            samples_[index] = static_cast<std::int16_t>(samples_[index] * 0.16f * fade);
        }
    }
    ~Audio() { if (device_) SDL_CloseAudioDevice(device_); }
    void Enable(bool enabled) {
        enabled_ = enabled;
        if (!enabled) { if (device_) SDL_ClearQueuedAudio(device_); return; }
        if (device_) return;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { SDL_Log("Audio unavailable: %s", SDL_GetError()); return; }
        SDL_AudioSpec format{};
        format.freq = 44100;
        format.format = AUDIO_S16SYS;
        format.channels = 1;
        format.samples = 512;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &format, nullptr, 0);
        if (!device_) SDL_Log("Audio unavailable: %s", SDL_GetError());
        else SDL_PauseAudioDevice(device_, 0);
    }
    void Click() {
        if (enabled_ && device_ && SDL_GetQueuedAudioSize(device_) < samples_.size() * sizeof(std::int16_t) * 2)
            SDL_QueueAudio(device_, samples_.data(), static_cast<Uint32>(samples_.size() * sizeof(std::int16_t)));
    }
    bool HasSamples() const {
        return std::any_of(samples_.begin(), samples_.end(), [](std::int16_t sample) { return sample != 0; });
    }
private:
    SDL_AudioDeviceID device_ = 0;
    bool enabled_ = false;
    std::vector<std::int16_t> samples_;
};

std::string FontPath() {
    char* base = SDL_GetBasePath();
    const std::filesystem::path directory = base ? base : ".";
    SDL_free(base);
    for (const auto& candidate : {directory / "../share/aterminal/unifont.hex", directory / "assets/unifont.hex",
                                   std::filesystem::path(ATERMINAL_ASSET_DIR) / "unifont.hex"}) {
        if (std::filesystem::exists(candidate)) return candidate.string();
    }
    throw std::runtime_error("Cannot find assets/unifont.hex");
}

class Application {
public:
    Application(Preferences preferences, Options options, std::string config)
        : preferences_(preferences), options_(std::move(options)), config_(std::move(config)), display_(FontPath()) {
        if (options_.shell.empty() || options_.shell[0] != '/') throw std::runtime_error("--shell must be an absolute executable path");
        window_.reset(SDL_CreateWindow("aterminal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options_.width, options_.height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (options_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) |
            (options_.maximized ? SDL_WINDOW_MAXIMIZED : 0)));
        if (!window_) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window_.get(), 400, 280);
        if (options_.self_test && !options_.maximized) {
            int width = 0, height = 0;
            SDL_GetWindowSize(window_.get(), &width, &height);
            const bool fullscreen = SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            if (fullscreen != options_.fullscreen || (!fullscreen && (width != options_.width || height != options_.height)))
                throw std::runtime_error("Startup size or fullscreen flag was not applied");
        }
        renderer_.reset(SDL_CreateRenderer(window_.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
        if (!renderer_) renderer_.reset(SDL_CreateRenderer(window_.get(), -1, SDL_RENDERER_SOFTWARE));
        if (!renderer_) throw std::runtime_error(SDL_GetError());
        Resize();
        Switch(options_.console - 1);
        audio_.Enable(preferences_.sound);
        SDL_StartTextInput();
    }

    int Run() {
        bool running = true;
        int frame = 0;
        const bool original_amber = preferences_.amber, original_sound = preferences_.sound;
        const int original_scale = preferences_.scale;
        const bool original_fullscreen = SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        struct ClipboardRestore {
            bool enabled;
            std::string saved;
            explicit ClipboardRestore(bool restore) : enabled(restore) {
                if (!enabled) return;
                char* text = SDL_GetClipboardText();
                if (text) saved = text;
                SDL_free(text);
            }
            ~ClipboardRestore() { if (enabled) SDL_SetClipboardText(saved.c_str()); }
        } clipboard_restore(options_.self_test);
        while (running) {
            if (options_.self_test) InjectTest(frame);
            SDL_Event event{};
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                else if (event.type == SDL_WINDOWEVENT) {
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) Resize();
                    dirty_ = true;
                } else if (event.type == SDL_KEYDOWN) Key(event.key);
                else if (event.type == SDL_TEXTINPUT && !settings_) {
                    TextInput(event.text.text);
                } else if (event.type == SDL_MOUSEWHEEL && !settings_) {
                    if (ReportingMouse()) MouseWheel(event.wheel.y);
                    else {
                        Current().Scroll(event.wheel.y * 3);
                        ClearSelection(); dirty_ = true;
                    }
                } else if (event.type == SDL_MOUSEBUTTONDOWN) MouseDown(event.button);
                else if (event.type == SDL_MOUSEMOTION && ReportingMouse()) MouseMotion(event.motion);
                else if (event.type == SDL_MOUSEBUTTONUP && reported_button_ >= 0 && ReportButton(event.button.button) == reported_button_) {
                    ReportMouse(reported_button_, event.button.x, event.button.y, true);
                    reported_button_ = -1;
                } else if (event.type == SDL_MOUSEMOTION && dragging_) {
                    const int index = IndexAt(event.motion.x, event.motion.y);
                    if (index != selection_anchor_ || selection_begin_ >= 0) {
                        selection_begin_ = selection_anchor_;
                        selection_end_ = index;
                    }
                    dirty_ = true;
                } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                    if (dragging_ && selection_begin_ >= 0) {
                        selection_end_ = IndexAt(event.button.x, event.button.y);
                        PublishSelection();
                    }
                    dragging_ = false;
                    dirty_ = true;
                }
            }
            if (options_.self_test) {
                const bool fullscreen = SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                if ((frame == 12 && preferences_.scale != original_scale + test_zoom_direction_) ||
                    (frame == 13 && preferences_.scale != original_scale) ||
                    (frame == 14 && preferences_.amber == original_amber) ||
                    (frame == 15 && preferences_.amber != original_amber) ||
                    (frame == 16 && preferences_.sound == original_sound) ||
                    (frame == 17 && preferences_.sound != original_sound) ||
                    ((frame == 19 || frame == 20) && fullscreen == original_fullscreen) ||
                    (frame == 21 && fullscreen != original_fullscreen))
                    throw std::runtime_error("SDL shortcut state transition failed");
                if (frame == 24 && (selection_begin_ != 0 || selection_end_ != 8))
                    throw std::runtime_error("Mouse drag selection failed");
                if (frame == 25) {
                    char* text = SDL_GetClipboardText();
                    const std::string copied = text ? text : "";
                    SDL_free(text);
                    if (copied != "ATERMINAL" || !test_input_.empty() || selection_begin_ >= 0)
                        throw std::runtime_error("Ctrl+C selection copy failed");
                }
                if ((frame == 26 && test_input_ != "ATERMINAL") ||
                    (frame == 27 && test_input_ != "ATERMINAL\x1a") ||
                    (frame == 28 && test_input_ != "ATERMINAL\x1a\x19") ||
                    (frame == 29 && test_input_ != "ATERMINAL\x1a\x19\x03") ||
                    (frame == 31 && test_input_ != "ATERMINAL\x1a\x19\x03\x03"))
                    throw std::runtime_error("Clipboard paste or terminal control-key forwarding failed");
                if ((frame == 36 && test_input_ != "ATERMINAL\x1a\x19\x03\x03+") ||
                    (frame == 37 && test_input_ != "ATERMINAL\x1a\x19\x03\x03+-") ||
                    ((frame == 36 || frame == 37) && preferences_.scale != original_scale))
                    throw std::runtime_error("Plain plus or minus did not type as text");
                if (frame == 40 && test_input_ != "ATERMINAL\x1a\x19\x03\x03+-ATERMINAL")
                    throw std::runtime_error("Middle click did not paste the selection");
#if SDL_VERSION_ATLEAST(2, 26, 0)
                if (frame == 41 && test_input_ != "ATERMINAL\x1a\x19\x03\x03+-ATERMINALEXTERNAL")
                    throw std::runtime_error("Middle click did not paste the system selection");
#endif
                if ((frame == 38 && preferences_.colour == test_colour_) || (frame == 39 && preferences_.colour != test_colour_))
                    throw std::runtime_error("Ctrl+Shift+O colour CRT toggle failed");
                if (frame >= 32 && frame <= 35) {
                    const int page = Current().terminal.Rows() - 1;
                    // Late shell output moves the anchored view by exactly the lines it adds.
                    const int history = static_cast<int>(Current().terminal.History().size());
                    if (frame == 32) test_history_ = history;
                    const int expected = (frame == 33 ? 2 * page : frame == 35 ? 0 : page) + history - test_history_;
                    if (Current().Scrollback() != expected || test_input_ != "ATERMINAL\x1a\x19\x03\x03")
                        throw std::runtime_error("Shift+PageUp/PageDown scrollback failed at frame " + std::to_string(frame) + ": offset " +
                                                 std::to_string(Current().Scrollback()) + ", expected " + std::to_string(expected) + ", history " +
                                                 std::to_string(history));
                }
            }
            for (std::size_t index = 0; index < sessions_.size(); ++index) {
                if (sessions_[index] && sessions_[index]->Pump() && static_cast<int>(index) == active_) dirty_ = true;
                if (sessions_[index] && sessions_[index]->terminal.TakeBell() && static_cast<int>(index) == active_) {
                    if (options_.bell == Bell::Visual) flash_until_ = SDL_GetTicks64() + 100;
                    else if (options_.bell == Bell::Audible) audio_.Click();
                    dirty_ = true;
                }
                if (sessions_[index] && !sessions_[index]->Running()) {
                    // 126 and 127 mean a command could not run, so keep its error readable.
                    const int status = sessions_[index]->ExitStatus();
                    if (options_.hold || status == 126 || status == 127) {
                        if (!exit_shown_[index]) {
                            exit_shown_[index] = true;
                            sessions_[index]->terminal.Feed("\r\n\033[0m[exited with status " + std::to_string(status) +
                                                            "; Ctrl+Shift+Enter restarts]\r\n");
                            dirty_ = true;
                        }
                    } else if (!options_.self_test && !CloseConsole(static_cast<int>(index))) running = false;
                }
            }
            const bool cursor = options_.no_blink || (SDL_GetTicks64() / 550) % 2 == 0;
            if (flash_until_ && SDL_GetTicks64() >= flash_until_) { flash_until_ = 0; dirty_ = true; }
            if (cursor != last_cursor_) { dirty_ = true; last_cursor_ = cursor; }
            if (dirty_) {
                display_.Paint(Current().terminal, Current().Scrollback(), preferences_, cursor || options_.self_test,
                               selection_begin_, selection_end_);
                if (settings_) display_.Settings(preferences_);
                display_.Effects(preferences_);
                if (SDL_GetTicks64() < flash_until_) {
                    auto* pixels = display_.image.data();
                    for (std::size_t index = 0; index < display_.image.size(); ++index) pixels[index] = static_cast<std::uint8_t>(std::min(255, pixels[index] + 24));
                }
                SDL_UpdateTexture(texture_.get(), nullptr, display_.image.data(), static_cast<int>(display_.image.width()) * 3);
                SDL_RenderClear(renderer_.get());
                SDL_RenderCopy(renderer_.get(), texture_.get(), nullptr, nullptr);
                SDL_RenderPresent(renderer_.get());
                UpdateTitle();
                dirty_ = false;
            }
            if (++frame == (options_.self_test ? 44 : options_.frames)) running = false;
            SDL_Delay(options_.self_test ? 10 : 8);
        }
        if (options_.self_test) {
            const auto count = std::count_if(sessions_.begin(), sessions_.end(), [](const auto& session) { return !!session; });
            if (count != 12 || preferences_.scale != original_scale || preferences_.amber != original_amber ||
                preferences_.sound != original_sound || active_ != 0 || !display_.Nonblank() ||
                !display_.HasGlyph(0x1f600) || !display_.HasGlyph(0x4e16) || !audio_.HasSamples())
                throw std::runtime_error("SDL interaction, font, rendering or audio-synthesis self-test failed");
            if (std::string(SDL_GetWindowTitle(window_.get())) != Title() || Title().rfind(options_.title + " / F1 / ", 0) != 0)
                throw std::runtime_error("Window title self-test failed");
            std::cout << "12 consoles, F-keys, zoom, fullscreen, title, mouse selection, clipboard, middle-click paste, Ctrl+C/V/Z/Y, Shift+PageUp/PageDown, phosphor, mute, Unicode glyphs and AdLib synthesis passed\n";
        }
        if (!options_.screenshot.empty() && !softrender::WritePng(display_.image, options_.screenshot))
            throw std::runtime_error("Could not write screenshot");
        if (!options_.self_test) Save();
        SDL_StopTextInput();
        return 0;
    }

private:
    struct WindowDeleter { void operator()(SDL_Window* value) const { SDL_DestroyWindow(value); } };
    struct RendererDeleter { void operator()(SDL_Renderer* value) const { SDL_DestroyRenderer(value); } };
    struct TextureDeleter { void operator()(SDL_Texture* value) const { SDL_DestroyTexture(value); } };
    Preferences preferences_;
    Options options_;
    std::string config_;
    Display display_;
    std::unique_ptr<SDL_Window, WindowDeleter> window_;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer_;
    std::unique_ptr<SDL_Texture, TextureDeleter> texture_;
    std::array<std::unique_ptr<Session>, 12> sessions_;
    Audio audio_;
    int active_ = 0, selection_begin_ = -1, selection_end_ = -1, selection_anchor_ = -1;
    bool dirty_ = true, settings_ = false, dragging_ = false, last_cursor_ = false;
    int reported_button_ = -1, reported_cell_ = -1;
    Uint64 flash_until_ = 0;
    std::string test_input_;
    int test_zoom_direction_ = 1;
    int test_history_ = 0;
    std::string selected_text_;
    bool test_colour_ = false;
    std::array<bool, 12> exit_shown_{};
    Session& Current() { return *sessions_[active_]; }

    void ClearSelection() { selection_begin_ = selection_end_ = selection_anchor_ = -1; dragging_ = false; }

    void Resize() {
        int width = 0, height = 0;
        SDL_GetRendererOutputSize(renderer_.get(), &width, &height);
        display_.Resize(width, height);
        texture_.reset(SDL_CreateTexture(renderer_.get(), SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                        display_.image.width(), display_.image.height()));
        if (!texture_) throw std::runtime_error(SDL_GetError());
        for (auto& session : sessions_)
            if (session) session->Resize(display_.Rows(preferences_), display_.Columns(preferences_));
        SDL_Rect rect{Display::Margin, Display::Margin, display_.CellWidth(preferences_), display_.CellHeight(preferences_)};
        SDL_SetTextInputRect(&rect);
        ClearSelection(); dirty_ = true;
    }

    // Closes an exited console and leaves it for the next open one. Returns false,
    // keeping the console, if it is the last one open.
    bool CloseConsole(int index) {
        const int count = static_cast<int>(sessions_.size());
        for (int offset = 1; offset < count; ++offset) {
            const int next = (index + offset) % count;
            if (!sessions_[next]) continue;
            sessions_[index].reset();
            if (index == active_) Switch(next);
            return true;
        }
        return false;
    }

    void Switch(int index) {
        if (!sessions_[index]) {
            // The startup command runs once, in the first console opened.
            sessions_[index] = std::make_unique<Session>(display_.Rows(preferences_), display_.Columns(preferences_), options_.shell, options_.command, options_.shell_options);
            options_.shell_options.startup.clear();
            sessions_[index]->terminal.SetHistoryLimit(options_.scrollback);
            exit_shown_[index] = false;
            // Nothing has been pumped yet, so the message lands ahead of all shell output.
            if (!options_.motd.empty()) sessions_[index]->terminal.Feed(MotdText(options_.motd));
        }
        active_ = index;
        settings_ = false; ClearSelection(); dirty_ = true;
        UpdateTitle();
    }

    std::string Title() const {
        return options_.title + " / F" + std::to_string(active_ + 1) + " / " + std::to_string(display_.Columns(preferences_)) +
               "x" + std::to_string(display_.Rows(preferences_));
    }

    void UpdateTitle() {
        const std::string title = Title();
        SDL_SetWindowTitle(window_.get(), title.c_str());
    }

    void ToggleFullscreen() {
        const bool fullscreen = SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (SDL_SetWindowFullscreen(window_.get(), fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
            SDL_Log("Could not toggle fullscreen: %s", SDL_GetError());
        Resize();
    }

    void Zoom(int direction) {
        preferences_.scale = std::clamp(preferences_.scale + direction, 1, Display::MaxScale);
        Resize();
    }

    void Input(std::string_view text) {
        if (options_.self_test) {
            // A shell may turn on bracketed paste; the checks compare the text itself.
            std::string input(text);
            for (const char* marker : {"\033[200~", "\033[201~"})
                for (auto at = input.find(marker); at != std::string::npos; at = input.find(marker)) input.erase(at, 6);
            test_input_ += input;
        }
        Current().Follow(); ClearSelection();
        Current().Send(text); dirty_ = true;
    }

    void Paste() {
        char* text = SDL_GetClipboardText();
        if (!text) return;
        const std::string contents = text;
        SDL_free(text);
        PasteText(contents);
    }

    // Middle click: the system-wide selection, then the last selection made here,
    // then the clipboard.
    void PasteSelection() {
        std::string contents;
#if SDL_VERSION_ATLEAST(2, 26, 0)
        if (char* text = SDL_GetPrimarySelectionText()) {
            contents = text;
            SDL_free(text);
        }
#endif
        if (contents.empty()) contents = selected_text_;
        if (contents.empty()) Paste();
        else PasteText(contents);
    }

    void TextInput(const std::string& text) {
        const SDL_Keymod held = SDL_GetModState();
        // The window shortcuts are chords, so they must not also type.
        if ((held & KMOD_CTRL) && (held & KMOD_SHIFT) && (held & KMOD_ALT)) return;
        if (held & KMOD_ALT) Current().Send("\033");
        Input(text);
    }

    void PasteText(const std::string& text) {
        std::string clean;
        for (const char* byte = text.c_str(); *byte && clean.size() < 1024 * 1024; ++byte) {
            if (*byte == '\n') clean += '\r';
            else if (static_cast<unsigned char>(*byte) >= 32 || *byte == '\t') clean += *byte;
        }
        if (Current().terminal.BracketedPaste()) clean = "\033[200~" + clean + "\033[201~";
        Input(clean);
    }

    void Copy() {
        if (selection_begin_ < 0) return;
        if (SDL_SetClipboardText(SelectedText().c_str()) == 0) ClearSelection();
        dirty_ = true;
    }

    void PublishSelection() {
        selected_text_ = SelectedText();
#if SDL_VERSION_ATLEAST(2, 26, 0)
        SDL_SetPrimarySelectionText(selected_text_.c_str());
#endif
    }

    std::string SelectedText() {
        int begin = std::min(selection_begin_, selection_end_), end = std::max(selection_begin_, selection_end_);
        std::string result, line;
        const auto& terminal = Current().terminal;
        const int history = static_cast<int>(terminal.History().size());
        for (int index = begin; index <= end; ++index) {
            const int row = index / terminal.Columns(), col = index % terminal.Columns();
            const int source = history - std::min(Current().Scrollback(), history) + row;
            if (source < history) {
                const auto& cells = terminal.History()[source];
                if (col < static_cast<int>(cells.size())) line += Terminal::Utf8(cells[col].text);
            } else line += Terminal::Utf8(terminal.At(source - history, col).text);
            if (col == terminal.Columns() - 1 || index == end) {
                while (!line.empty() && line.back() == ' ') line.pop_back();
                result += line; line.clear();
                if (index != end) result += '\n';
            }
        }
        return result;
    }

    int IndexAt(int mouse_x, int mouse_y) {
        const auto [col, row] = CellAt(mouse_x, mouse_y);
        return row * Current().terminal.Columns() + col;
    }

    std::pair<int, int> CellAt(int mouse_x, int mouse_y) {
        int width = 0, height = 0;
        SDL_GetWindowSize(window_.get(), &width, &height);
        const int pixel_x = mouse_x * static_cast<int>(display_.image.width()) / std::max(width, 1);
        const int pixel_y = mouse_y * static_cast<int>(display_.image.height()) / std::max(height, 1);
        const int col = std::clamp((pixel_x - Display::Margin) / display_.CellWidth(preferences_), 0, Current().terminal.Columns() - 1);
        const int row = std::clamp((pixel_y - Display::Margin) / display_.CellHeight(preferences_), 0, Current().terminal.Rows() - 1);
        return {col, row};
    }

    // Shift bypasses the application so selection and paste still work.
    bool ReportingMouse() {
        return !settings_ && Current().terminal.MouseMode() != 0 && !(SDL_GetModState() & KMOD_SHIFT);
    }

    static int ReportButton(int sdl_button) {
        if (sdl_button == SDL_BUTTON_LEFT) return 0;
        if (sdl_button == SDL_BUTTON_MIDDLE) return 1;
        if (sdl_button == SDL_BUTTON_RIGHT) return 2;
        return -1;
    }

    // button: 0-2 press, 3 release without a button (legacy), +32 motion, 64/65 wheel.
    void ReportMouse(int button, int mouse_x, int mouse_y, bool release) {
        const auto [col, row] = CellAt(mouse_x, mouse_y);
        const SDL_Keymod held = SDL_GetModState();
        int code = button;
        if (held & KMOD_ALT) code += 8;
        if (held & KMOD_CTRL) code += 16;
        const int x = col + 1, y = row + 1;
        std::string report;
        if (Current().terminal.MouseSgr()) {
            report = "\033[<" + std::to_string(code) + ";" + std::to_string(x) + ";" + std::to_string(y) + (release ? "m" : "M");
        } else {
            if (x > 223 || y > 223) return;
            if (release) code = (code & ~3) | 3;
            report = "\033[M";
            report += static_cast<char>(32 + code);
            report += static_cast<char>(32 + x);
            report += static_cast<char>(32 + y);
        }
        reported_cell_ = row * Current().terminal.Columns() + col;
        Current().Send(report);
    }

    void MouseMotion(const SDL_MouseMotionEvent& event) {
        const int mode = Current().terminal.MouseMode();
        if (mode == 1000 || (mode == 1002 && reported_button_ < 0)) return;
        if (IndexAt(event.x, event.y) == reported_cell_) return;
        ReportMouse((reported_button_ >= 0 ? reported_button_ : 3) + 32, event.x, event.y, false);
    }

    void MouseWheel(int amount) {
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        for (int i = 0; i < std::min(std::abs(amount), 10); ++i)
            ReportMouse(amount > 0 ? 64 : 65, x, y, false);
    }

    void MouseDown(const SDL_MouseButtonEvent& event) {
        if (ReportingMouse()) {
            const int button = ReportButton(event.button);
            if (button >= 0) {
                reported_button_ = button;
                ReportMouse(button, event.x, event.y, false);
            }
            return;
        }
        if (event.button == SDL_BUTTON_RIGHT) { settings_ = !settings_; dirty_ = true; return; }
        if (settings_) {
            int width = 0, height = 0;
            SDL_GetWindowSize(window_.get(), &width, &height);
            const int px = event.x * static_cast<int>(display_.image.width()) / std::max(width, 1);
            const int py = event.y * static_cast<int>(display_.image.height()) / std::max(height, 1);
            const int left = std::max(8, (static_cast<int>(display_.image.width()) - Display::SettingsWidth) / 2);
            const int top = std::max(8, (static_cast<int>(display_.image.height()) - Display::SettingsHeight) / 2);
            if (px < left || px > left + Display::SettingsWidth || py < top || py > top + Display::SettingsHeight) settings_ = false;
            else if (py >= top + 60 && py < top + 300) {
                const int row = (py - top - 60) / 40;
                if (row == 0) { preferences_.amber = false; preferences_.colour = false; }
                else if (row == 1) { preferences_.amber = true; preferences_.colour = false; }
                else if (row == 2) preferences_.colour = true;
                else if (row == 3) { preferences_.sound = !preferences_.sound; audio_.Enable(preferences_.sound); }
                else if (row == 4) preferences_.crt = !preferences_.crt;
                else if (row == 5) preferences_.bitmap = !preferences_.bitmap;
            }
            dirty_ = true; return;
        }
        if (event.button == SDL_BUTTON_MIDDLE) PasteSelection();
        else if (event.button == SDL_BUTTON_LEFT) {
            ClearSelection();
            selection_anchor_ = IndexAt(event.x, event.y);
            dragging_ = true; dirty_ = true;
        }
    }

    void Key(const SDL_KeyboardEvent& event) {
        const SDL_Keycode key = event.keysym.sym;
        const bool control = event.keysym.mod & KMOD_CTRL, shift = event.keysym.mod & KMOD_SHIFT;
        if ((event.keysym.mod & KMOD_ALT) && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            if (!event.repeat) ToggleFullscreen();
            return;
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) { Switch(static_cast<int>(key - SDLK_F1)); audio_.Click(); return; }
        if (control && shift) {
            if (key == SDLK_a) { preferences_.amber = true; preferences_.colour = false; }
            else if (key == SDLK_g) { preferences_.amber = false; preferences_.colour = false; }
            else if (key == SDLK_o) preferences_.colour = !preferences_.colour;
            else if (key == SDLK_s) { preferences_.sound = !preferences_.sound; audio_.Enable(preferences_.sound); }
            else if (key == SDLK_r) preferences_.crt = !preferences_.crt;
            else if (key == SDLK_b) preferences_.bitmap = !preferences_.bitmap;
            else if (key == SDLK_p) settings_ = !settings_;
            else if (key == SDLK_c) Copy();
            else if (key == SDLK_v) Paste();
            else if ((event.keysym.mod & KMOD_ALT) && (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS)) Zoom(1);
            else if ((event.keysym.mod & KMOD_ALT) && (key == SDLK_MINUS || key == SDLK_KP_MINUS)) Zoom(-1);
            else if (key == SDLK_f) {
                if (!event.repeat) ToggleFullscreen();
            } else if (key == SDLK_RETURN && !Current().Running()) {
                sessions_[active_].reset(); Switch(active_);
            } else return;
            dirty_ = true; return;
        }
        if (settings_) { if (key == SDLK_ESCAPE) { settings_ = false; dirty_ = true; } return; }
        audio_.Click();
        if (control && !(event.keysym.mod & KMOD_ALT)) {
            if (key == SDLK_c && selection_begin_ >= 0) { Copy(); return; }
            if (key == SDLK_v) { Paste(); return; }
        }
        if (shift && (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)) {
            Current().Scroll((key == SDLK_PAGEUP ? 1 : -1) * (Current().terminal.Rows() - 1));
            ClearSelection(); dirty_ = true; return;
        }
        if (shift && key == SDLK_INSERT) { Paste(); return; }
        if (control && key >= SDLK_a && key <= SDLK_z) {
            Input(std::string(1, static_cast<char>(key - SDLK_a + 1))); return;
        }
        if (control && key == SDLK_SPACE) { Input(std::string(1, '\0')); return; }
        if (control && key == SDLK_LEFTBRACKET) { Input("\033"); return; }
        if (control && key == SDLK_BACKSLASH) { Input(std::string(1, '\x1c')); return; }
        if (control && key == SDLK_RIGHTBRACKET) { Input(std::string(1, '\x1d')); return; }
        const char* sequence = nullptr;
        char arrow = 0;
        if (key == SDLK_UP) arrow = 'A';
        else if (key == SDLK_DOWN) arrow = 'B';
        else if (key == SDLK_RIGHT) arrow = 'C';
        else if (key == SDLK_LEFT) arrow = 'D';
        else if (key == SDLK_HOME) arrow = 'H';
        else if (key == SDLK_END) arrow = 'F';
        if (arrow) {
            const int modifier = 1 + (shift ? 1 : 0) + ((event.keysym.mod & KMOD_ALT) ? 2 : 0) + (control ? 4 : 0);
            std::string bytes = modifier > 1 ? "\033[1;" + std::to_string(modifier) :
                (Current().terminal.ApplicationCursor() ? "\033O" : "\033[");
            Input(bytes + arrow); return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) sequence = "\r";
        else if (key == SDLK_BACKSPACE) sequence = "\x7f";
        else if (key == SDLK_TAB) sequence = shift ? "\033[Z" : "\t";
        else if (key == SDLK_ESCAPE) sequence = "\033";
        else if (key == SDLK_INSERT) sequence = "\033[2~";
        else if (key == SDLK_DELETE) sequence = "\033[3~";
        else if (key == SDLK_PAGEUP) sequence = "\033[5~";
        else if (key == SDLK_PAGEDOWN) sequence = "\033[6~";
        if (sequence) Input(sequence);
    }

    void InjectTest(int frame) {
        SDL_Event event{};
        event.type = SDL_KEYDOWN;
        if (frame < 12) event.key.keysym.sym = SDLK_F1 + frame;
        else if (frame == 12 || frame == 13) {
            if (frame == 12) test_zoom_direction_ = preferences_.scale == Display::MaxScale ? -1 : 1;
            const int direction = frame == 12 ? test_zoom_direction_ : -test_zoom_direction_;
            event.key.keysym.mod = KMOD_CTRL | KMOD_SHIFT | KMOD_ALT;
            event.key.keysym.sym = direction > 0 ? SDLK_EQUALS : SDLK_MINUS;
        } else if (frame >= 14 && frame <= 17) {
            event.key.keysym.mod = KMOD_CTRL | KMOD_SHIFT;
            event.key.keysym.sym = frame < 16 ? (preferences_.amber ? SDLK_g : SDLK_a) : SDLK_s;
        } else if (frame == 18) event.key.keysym.sym = SDLK_F1;
        else if (frame >= 19 && frame <= 21) {
            event.key.keysym.mod = KMOD_ALT;
            event.key.keysym.sym = frame == 21 ? SDLK_KP_ENTER : SDLK_RETURN;
            event.key.repeat = frame == 20 ? 1 : 0;
        }
        else if (frame >= 22 && frame <= 24) {
            int width = 0, height = 0;
            SDL_GetWindowSize(window_.get(), &width, &height);
            const int column = frame == 22 ? 0 : 8;
            const int mouse_x = (Display::Margin + column * display_.CellWidth(preferences_) + display_.CellWidth(preferences_) / 2) * width / display_.image.width();
            const int mouse_y = (Display::Margin + display_.CellHeight(preferences_) / 2) * height / display_.image.height();
            if (frame == 22) {
                test_input_.clear();
                Current().terminal.Feed("\0337\033[HATERMINAL\0338");
                SDL_SetClipboardText("previous clipboard");
            }
            if (frame == 23) {
                event.type = SDL_MOUSEMOTION;
                event.motion.x = mouse_x; event.motion.y = mouse_y;
                event.motion.state = SDL_BUTTON_LMASK;
            } else {
                event.type = frame == 22 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = mouse_x; event.button.y = mouse_y;
            }
        }
        else if ((frame >= 25 && frame <= 29) || frame == 31) {
            event.key.keysym.mod = KMOD_CTRL;
            event.key.keysym.sym = frame == 26 ? SDLK_v : frame == 27 ? SDLK_z : frame == 28 ? SDLK_y : SDLK_c;
        }
        else if (frame == 30) {
            event.type = SDL_MOUSEBUTTONDOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = event.button.y = 30;
            SDL_PushEvent(&event);
            event.type = SDL_MOUSEBUTTONUP;
        }
        else if (frame == 40 || frame == 41) {
            // The selection from frame 24 wins over the clipboard, then one from elsewhere.
            SDL_SetClipboardText("CLIPBOARD");
#if SDL_VERSION_ATLEAST(2, 26, 0)
            if (frame == 41) SDL_SetPrimarySelectionText("EXTERNAL");
#endif
            event.type = SDL_MOUSEBUTTONDOWN;
            event.button.button = SDL_BUTTON_MIDDLE;
        }
        else if (frame == 38 || frame == 39) {
            if (frame == 38) test_colour_ = preferences_.colour;
            event.key.keysym.mod = KMOD_CTRL | KMOD_SHIFT;
            event.key.keysym.sym = SDLK_o;
        }
        else if (frame == 36 || frame == 37) {
            // Called directly because sdl2-compat cannot push a text event through SDL3.
            TextInput(frame == 36 ? "+" : "-");
            return;
        }
        else if (frame >= 32 && frame <= 35) {
            if (frame == 32) {
                std::string lines = "\033[999;1H";
                for (int line = 0; line < Current().terminal.Rows() * 3; ++line) lines += "scrollback test\r\n";
                Current().terminal.Feed(lines);
            }
            event.key.keysym.mod = KMOD_SHIFT;
            event.key.keysym.sym = frame < 34 ? SDLK_PAGEUP : SDLK_PAGEDOWN;
        }
        else return;
        SDL_PushEvent(&event);
    }

    void Save() {
        if (config_.empty()) return;
        const std::string temporary = config_ + "." + std::to_string(getpid()) + ".tmp";
        std::ofstream file(temporary);
        file << preferences_.amber << ' ' << preferences_.sound << ' ' << preferences_.crt << ' '
             << preferences_.bitmap << ' ' << preferences_.colour << '\n';
        file.close();
        if (file) {
            std::error_code error;
            std::filesystem::rename(temporary, config_, error);
            if (error) SDL_Log("Could not save preferences: %s", error.message().c_str());
        }
    }
};

const char* DemoCommand() {
    return "printf '\033[2J\033[H\033[1mATERMINAL  /  PHOSPHOR WORKSTATION\033[0m\r\n\r\n"
           "  UNIX SESSION                         VT100 / UTF-8\r\n"
           "  --------------------------------------------------\r\n\r\n"
           "  The quick brown fox jumps over the lazy dog.\r\n"
           "  ABCDEFGHIJKLMNOPQRSTUVWXYZ  0123456789\r\n"
           "  \033[1mBold Mixed Case\033[0m  \033[3mItalic Mixed Case\033[0m  \033[1;3mBoth\033[0m\r\n"
           "  !@#$%%^&*()_+-=[]{};:\"<>/? \\ |\r\n\r\n"
           "  Unicode   caf\303\251  \316\273  \342\206\222  \344\270\226\347\225\214\r\n"
           "  Emoji     \360\237\230\200  \360\237\232\200  \360\237\222\273  \342\231\245\r\n\r\n"
           "\033(0  lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk\r\n"
           "  x                                x\r\n"
           "  mqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqj\033(B\r\n\r\n'; exec /bin/sh -i";
}
}

int main(int argc, char** argv) {
    try {
        Preferences preferences;
        Options options;
        char* directory = SDL_GetPrefPath("ross-newman", "aterminal");
        const std::string config = directory ? std::string(directory) + "preferences" : "";
        SDL_free(directory);
        std::vector<std::string> args(argv, argv + argc);
        bool testing = false, text_size_given = false, geometry_given = false, prompt_chosen = false;
        int config_files = 0;
        for (std::size_t index = 1; index < args.size(); ++index) if (args[index] == "--self-test") testing = true;
        if (!testing) {
            std::ifstream file(config);
            file >> preferences.amber >> preferences.sound >> preferences.crt >> preferences.bitmap >> preferences.colour;
        }
        for (std::size_t index = 1; index < args.size(); ++index) {
            const std::string raw = args[index];
            if (raw == "--") {
                options.shell_options.exec.assign(args.begin() + index + 1, args.end());
                if (options.shell_options.exec.empty()) throw std::runtime_error("-- must be followed by a program");
                break;
            }
            const auto equal = raw.find('=');
            const bool inline_value = equal != std::string::npos;
            const std::string argument = raw.substr(0, equal);
            const bool takes_value = argument == "--size" || argument == "--width" || argument == "--height" ||
                argument == "--text-size" || argument == "--colour" || argument == "--color" ||
                argument == "--shell" || argument == "--command" || argument == "-e" || argument == "-c" ||
                argument == "--startup" || argument == "--screenshot" || argument == "--frames" ||
                argument == "--geometry" || argument == "-d" || argument == "--working-directory" ||
                argument == "--title" || argument == "--scrollback" || argument == "--cursor" ||
                argument == "--console" || argument == "--scanlines" || argument == "--bloom" ||
                argument == "--bell" || argument == "--config" || argument == "--prompt";
            if (inline_value && !takes_value && argument != "--fullscreen" && argument != "--motd")
                throw std::runtime_error(argument + " does not accept a value");
            const auto value = [&]() -> std::string {
                if (inline_value) return raw.substr(equal + 1);
                if (index + 1 >= args.size()) throw std::runtime_error("Missing value for " + argument);
                return args[++index];
            };
            if (argument == "--amber") { preferences.amber = true; preferences.colour = false; }
            else if (argument == "--green") { preferences.amber = false; preferences.colour = false; }
            else if (argument == "--no-sound") preferences.sound = false;
            else if (argument == "--sound") preferences.sound = true;
            else if (argument == "--no-crt") preferences.crt = false;
            else if (argument == "--bitmap") preferences.bitmap = true;
            else if (argument == "--fullscreen") {
                const std::string enabled = inline_value ? value() : "true";
                if (enabled != "true" && enabled != "false" && enabled != "1" && enabled != "0")
                    throw std::runtime_error("--fullscreen must be true or false");
                options.fullscreen = enabled == "true" || enabled == "1";
            }
            else if (argument == "--windowed") options.fullscreen = false;
            else if (argument == "--size") {
                const std::string dimensions = value();
                const auto separator = dimensions.find_first_of("xX");
                if (separator == std::string::npos) throw std::runtime_error("--size must be WIDTHxHEIGHT in pixels");
                options.width = IntegerFlag(dimensions.substr(0, separator), 400, 4096, "--size width");
                options.height = IntegerFlag(dimensions.substr(separator + 1), 280, 2160, "--size height");
            }
            else if (argument == "--width") options.width = IntegerFlag(value(), 400, 4096, argument);
            else if (argument == "--height") options.height = IntegerFlag(value(), 280, 2160, argument);
            else if (argument == "--text-size") { preferences.scale = IntegerFlag(value(), 1, Display::MaxScale, argument); text_size_given = true; }
            else if (argument == "--colour" || argument == "--color") {
                const std::string colour = value();
                if (colour != "green" && colour != "amber" && colour != "colour" && colour != "color")
                    throw std::runtime_error(argument + " must be green, amber or colour");
                preferences.amber = colour == "amber";
                preferences.colour = colour == "colour" || colour == "color";
            }
            else if (argument == "--shell") options.shell = value();
            else if (argument == "--command" || argument == "-e") options.command = value();
            else if (argument == "-c" || argument == "--startup") {
                options.shell_options.startup = value();
                if (options.shell_options.startup.size() > kMaxStartupCommand) throw std::runtime_error(argument + " command is too long");
            }
            else if (argument == "--prompt") {
                const std::string name = value();
                if (name == "list") {
                    for (const Prompt& prompt : Prompts()) {
                        std::string label = prompt.name;
                        label.resize(std::max<std::size_t>(label.size(), 10), ' ');
                        std::cout << label << ' ' << prompt.summary
                                  << (label.rfind(kDefaultPrompt, 0) == 0 ? "  (default)" : "") << '\n';
                    }
                    std::cout << "none       Keep the prompt from your shell startup files\n"
                                 "Anything containing a symbol or space is used as the prompt itself.\n";
                    SDL_Quit(); return 0;
                }
                if (name != "none" && !PromptExists(name) && !IsCustomPrompt(name)) throw std::runtime_error("Unknown prompt: " + name + " (see --prompt=list; custom text needs a symbol or space)");
                options.shell_options.prompt = name == "none" ? "" : name;
                prompt_chosen = true;
            }
            else if (argument == "--geometry") {
                const std::string dimensions = value();
                const auto separator = dimensions.find_first_of("xX");
                if (separator == std::string::npos) throw std::runtime_error("--geometry must be COLUMNSxROWS in characters");
                options.columns = IntegerFlag(dimensions.substr(0, separator), 20, 500, "--geometry columns");
                options.rows = IntegerFlag(dimensions.substr(separator + 1), 5, 200, "--geometry rows");
                geometry_given = true;
            }
            else if (argument == "-d" || argument == "--working-directory") {
                options.shell_options.directory = value();
                if (!std::filesystem::is_directory(options.shell_options.directory))
                    throw std::runtime_error("Not a directory: " + options.shell_options.directory);
            }
            else if (argument == "--title") options.title = value();
            else if (argument == "--hold") options.hold = true;
            else if (argument == "-l" || argument == "--login") options.shell_options.login = true;
            else if (argument == "--scrollback") options.scrollback = IntegerFlag(value(), 0, 1000000, argument);
            else if (argument == "--cursor") {
                const std::string style = value();
                if (style == "block") preferences.cursor = Preferences::Cursor::Block;
                else if (style == "bar") preferences.cursor = Preferences::Cursor::Bar;
                else if (style == "underline") preferences.cursor = Preferences::Cursor::Underline;
                else throw std::runtime_error("--cursor must be block, bar or underline");
            }
            else if (argument == "--no-blink") options.no_blink = true;
            else if (argument == "--console") options.console = IntegerFlag(value(), 1, 12, argument);
            else if (argument == "--maximized") options.maximized = true;
            else if (argument == "--scanlines") preferences.scanlines = IntegerFlag(value(), 0, 100, argument);
            else if (argument == "--bloom") preferences.bloom = IntegerFlag(value(), 0, 100, argument);
            else if (argument == "--bell") {
                const std::string bell = value();
                if (bell == "visual") options.bell = Bell::Visual;
                else if (bell == "audible") options.bell = Bell::Audible;
                else if (bell == "none") options.bell = Bell::None;
                else throw std::runtime_error("--bell must be visual, audible or none");
            }
            else if (argument == "--config") {
                if (++config_files > kMaxConfigFiles) throw std::runtime_error("Too many config files");
                const auto lines = ConfigArguments(value());
                args.insert(args.begin() + index + 1, lines.begin(), lines.end());
            }
            else if (argument == "--screenshot") options.screenshot = value();
            else if (argument == "--frames") options.frames = IntegerFlag(value(), 1, 1000000, argument);
            else if (argument == "--demo") options.command = DemoCommand();
            else if (argument == "--motd") {
                std::string name = "random";
                if (inline_value) name = value();
                else if (index + 1 < args.size() && args[index + 1][0] != '-') name = args[++index];
                if (name == "list") {
                    std::cout << "random    A different message on each console\n";
                    for (const Motd& message : Motds()) {
                        std::string label = message.name;
                        label.resize(std::max<std::size_t>(label.size(), 9), ' ');
                        std::cout << label << ' ' << message.summary << '\n';
                    }
                    SDL_Quit(); return 0;
                }
                if (!MotdExists(name)) throw std::runtime_error("Unknown MOTD: " + name + " (see --motd=list)");
                options.motd = name;
            }
            else if (argument == "--self-test") { options.self_test = true; options.command = DemoCommand(); }
            else if (argument == "--help" || argument == "-h") {
                std::cout << "aterminal [--green|--amber] [--no-sound|--sound] [--no-crt] [--bitmap]\n"
                    "          [--size WIDTHxHEIGHT] [--width N --height N] [--text-size 1..6]\n"
                    "          [--colour green|amber|colour] [--fullscreen[=true|false]|--windowed]\n"
                    "          [--shell /path/to/shell] [-e 'command'] [-c 'command'] [--demo]\n"
                    "          [--geometry COLUMNSxROWS] [-d DIR] [--title TEXT] [--maximized]\n"
                    "          [--console 1..12] [--scrollback N] [-l|--login] [--hold]\n"
                    "          [--cursor block|bar|underline] [--no-blink]\n"
                    "          [--scanlines 0..100] [--bloom 0..100] [--bell visual|audible|none]\n"
                    "          [--prompt STYLE|list] [--config FILE] [-- program args...]\n"
                    "          [--motd[=NAME|random|list]]\n"
                    "          [-h|--help]\n"
                    "Value flags accept --flag=value or --flag value; --color aliases --colour.\n"
                    "F1-F12: consoles; Ctrl+Shift+Alt+Plus/Minus: zoom\n"
                    "Alt+Enter: fullscreen; Ctrl+Shift+F: fullscreen (alternative)\n"
                    "Ctrl+Shift+A/G: amber/green; O: colour CRT; S: sound; R: CRT; B: bitmap\n"
                    "Ctrl+C: copy selection, otherwise interrupt; Ctrl+V: paste; middle click: paste selection\n"
                    "Ctrl+Z/Y: forwarded to shell/app (usually suspend/yank in shells)\n"
                    "Ctrl+Shift+P or right click: setup; C/V: copy/paste; Enter: restart a held shell\n"
                    "exit closes the console, and the last console closes aterminal\n"
                    "Shift+PageUp/PageDown or wheel: scrollback; drag: selection\n";
                SDL_Quit(); return 0;
            } else throw std::runtime_error("Unknown option: " + argument);
        }
        const ShellOptions& shell_options = options.shell_options;
        if (!options.self_test) {
            if (!shell_options.startup.empty() && !options.command.empty())
                throw std::runtime_error("-c cannot be used with -e or --demo");
            if (!shell_options.exec.empty() && (!options.command.empty() || !shell_options.startup.empty()))
                throw std::runtime_error("-- cannot be used with -e, -c or --demo");
            if (!shell_options.prompt.empty() && (!IsBash(options.shell) || !options.command.empty() || !shell_options.exec.empty()))
                throw std::runtime_error("--prompt needs an interactive bash shell");
        }
        if (!prompt_chosen && IsBash(options.shell) && options.command.empty() && shell_options.exec.empty())
            options.shell_options.prompt = kDefaultPrompt;
        if (geometry_given && (options.width != 0 || options.height != 0))
            throw std::runtime_error("--geometry cannot be used with --size, --width or --height");
        if (!options.self_test && !options.motd.empty() && options.command == DemoCommand())
            throw std::runtime_error("--motd cannot be used with --demo, which clears the screen");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) throw std::runtime_error(SDL_GetError());
        const auto default_width = [&] { return options.columns * Display::CellWidth(preferences) + Display::Margin * 2; };
        const auto default_height = [&] { return options.rows * Display::CellHeight(preferences) + Display::Margin * 2; };
        // Keep the default console whole on smaller screens by lowering the zoom.
        SDL_Rect usable{};
        if (!text_size_given && options.width == 0 && options.height == 0 && SDL_GetDisplayUsableBounds(0, &usable) == 0)
            while (preferences.scale > 1 && (default_width() > usable.w || default_height() > usable.h)) --preferences.scale;
        if (options.width == 0) options.width = std::max(default_width(), 400);
        if (options.height == 0) options.height = std::max(default_height(), 280);
        if (options.width > 4096 || options.height > 2160)
            throw std::runtime_error("Window would be larger than 4096x2160; use a smaller --geometry or --text-size");
        int result = 0;
        { Application application(preferences, options, options.self_test ? "" : config); result = application.Run(); }
        SDL_Quit();
        return result;
    } catch (const std::exception& error) {
        std::cerr << "aterminal: " << error.what() << '\n';
        SDL_Quit(); return 1;
    }
}