#pragma once

#include <string>
#include <vector>

struct IdentifierSourceToken
{
    std::string text;
    bool isIdentifier = false;
};

// Returns one entry per source token. An empty entry means that the original
// spelling must be preserved; otherwise the entry contains the replacement.
std::vector<std::string> ResolveIdentifierNames(
    const std::vector<IdentifierSourceToken>& sourceTokens);
