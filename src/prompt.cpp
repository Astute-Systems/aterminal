#include "prompt.h"

#include <stdexcept>

const std::vector<Prompt>& Prompts() {
    static const std::vector<Prompt> prompts{
        {"slackware", "user@host:~/dir$   Early Linux, 1993", R"(\u@\h:\w)", R"(\$ )"},
        {"debian", "host:~/dir$        Early Debian", R"(\h:\w)", R"(\$ )"},
        {"redhat", "[user@host dir]$   Classic Red Hat", R"([\u@\h \W])", R"(\$ )"},
        {"bsd", "host%              BSD csh", R"(\h)", "% "},
        {"sunos", "host 12%           SunOS csh with history number", R"(\h \!)", "% "},
        {"unix", "$                  Basic UNIX prompt", "", R"(\$ )"},
        {"bourne", "$                  Original Unix shell", "", R"(\$ )"},
        {"bash", "bash-5.2$          Stock bash, no startup files", R"(\s-\v)", R"(\$ )"},
        {"dos", "C:\\HOME\\USER>      MS-DOS", "$(__aterminal_dos)", ">"},
    };
    return prompts;
}

bool PromptExists(const std::string& name) {
    for (const Prompt& prompt : Prompts())
        if (name == prompt.name) return true;
    return false;
}

bool IsCustomPrompt(const std::string& text) {
    return !PromptExists(text) && text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos;
}

std::string PromptScript(const std::string& name) {
    if (IsCustomPrompt(name)) {
        std::string script = "PS1='";
        for (char byte : name) {
            if (byte == '\'') script += "'\\''";
            else script += byte;
        }
        return script + (name.back() == ' ' ? "'\n" : " '\n");
    }
    for (const Prompt& prompt : Prompts()) {
        if (name != prompt.name) continue;
        std::string script;
        if (name == "dos") script += R"(__aterminal_dos() { local path=${PWD^^}; printf '%s' "C:${path//\//\\}"; })" "\n";
        script += "PS1='";
        if (*prompt.body) script += std::string(R"(\[\033[1m\])") + prompt.body + R"(\[\033[0m\])";
        return script + prompt.symbol + "'\n";
    }
    throw std::invalid_argument("Unknown prompt: " + name);
}
