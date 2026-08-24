#pragma once

#include <iosfwd>
#include <string>

enum class TokenEmissionKind {
    Lexical,
    Trivia,
    LiteralFragment,
    PreprocessorDirective,
};

class TokenEmitter
{
public:
    explicit TokenEmitter(std::ostream& output);

    void write(const std::string& text, TokenEmissionKind kind);
    bool good() const;

private:
    std::ostream&     output_;
    std::string       previousText_;
    TokenEmissionKind previousKind_ = TokenEmissionKind::Trivia;
    bool              hasPrevious_  = false;
};
