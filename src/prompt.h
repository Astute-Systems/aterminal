#pragma once

#include <string>
#include <vector>

struct Prompt {
    const char* name;
    const char* summary;
    const char* body;   // Bash PS1 text shown bold
    const char* symbol; // Bash PS1 text shown at normal weight
};

// The built-in styles, in the order --prompt=list prints them.
const std::vector<Prompt>& Prompts();

bool PromptExists(const std::string& name);

// True for text to show as the prompt itself. Plain words are not custom text, so
// a mistyped style name is reported rather than becoming the prompt.
bool IsCustomPrompt(const std::string& text);

// Bash commands that select a style or custom text. Throws std::invalid_argument
// for anything else.
std::string PromptScript(const std::string& name);
