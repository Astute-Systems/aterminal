#pragma once

#include <string>
#include <vector>

struct Motd {
    const char* name;
    const char* summary;
    const char* text;
};

// The built-in messages, in the order --motd=list prints them.
const std::vector<Motd>& Motds();

// True for a built-in message name or "random".
bool MotdExists(const std::string& name);

// Terminal-ready text for a name accepted by MotdExists; "random" picks a new
// message on every call. Throws std::invalid_argument for any other name.
std::string MotdText(const std::string& name);
