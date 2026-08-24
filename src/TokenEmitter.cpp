#include "TokenEmitter.hpp"

#include <ostream>
#include <string>

#ifdef OPEN_SLEX_HAS_LIBTOOLING_FRONTEND
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#endif

namespace
{
#ifdef OPEN_SLEX_HAS_LIBTOOLING_FRONTEND
struct RawToken
{
    clang::tok::TokenKind kind   = clang::tok::unknown;
    unsigned int          length = 0;
};

bool lexSingleToken(const std::string& spelling, RawToken& result)
{
    if (spelling.empty())
    {
        return false;
    }

    clang::LangOptions languageOptions;
    languageOptions.C99       = true;
    languageOptions.OpenCL    = true;
    languageOptions.Trigraphs = true;

    const char*  begin = spelling.data();
    clang::Lexer lexer(clang::SourceLocation(), languageOptions, begin, begin, begin + spelling.size());

    clang::Token token;
    lexer.LexFromRawLexer(token);
    if (token.is(clang::tok::eof))
    {
        return false;
    }

    clang::Token end;
    lexer.LexFromRawLexer(end);
    if (!end.is(clang::tok::eof))
    {
        return false;
    }

    result.kind   = token.getKind();
    result.length = token.getLength();
    return result.length == spelling.size();
}

bool requiresSeparator(const std::string& previous, const std::string& current)
{
    RawToken previousToken;
    RawToken currentToken;
    if (!lexSingleToken(previous, previousToken) || !lexSingleToken(current, currentToken))
    {
        return true;
    }

    const std::string  combined = previous + current;
    clang::LangOptions languageOptions;
    languageOptions.C99       = true;
    languageOptions.OpenCL    = true;
    languageOptions.Trigraphs = true;

    const char*  begin = combined.data();
    clang::Lexer lexer(clang::SourceLocation(), languageOptions, begin, begin, begin + combined.size());

    clang::Token first;
    clang::Token second;
    clang::Token end;
    lexer.LexFromRawLexer(first);
    lexer.LexFromRawLexer(second);
    lexer.LexFromRawLexer(end);

    return first.getKind() != previousToken.kind || first.getLength() != previousToken.length || second.getKind() != currentToken.kind ||
           second.getLength() != currentToken.length || !end.is(clang::tok::eof);
}
#else
bool isIdentifierCharacter(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || character == '_';
}

bool requiresSeparator(const std::string& previous, const std::string& current)
{
    if (previous.empty() || current.empty())
    {
        return false;
    }
    if (isIdentifierCharacter(previous.back()) && isIdentifierCharacter(current.front()))
    {
        return true;
    }

    const std::string boundary = std::string(1, previous.back()) + current.front();
    return boundary == "++" || boundary == "--" || boundary == "->" || boundary == "<<" || boundary == ">>" || boundary == "<=" || boundary == ">=" || boundary == "==" ||
           boundary == "!=" || boundary == "&&" || boundary == "||" || boundary == "*=" || boundary == "/=" || boundary == "%=" || boundary == "+=" || boundary == "-=" ||
           boundary == "&=" || boundary == "^=" || boundary == "|=" || boundary == "/*" || boundary == "//" || boundary == "<:" || boundary == ":>" || boundary == "<%" ||
           boundary == "%>" || boundary == "%:" || boundary == "??";
}
#endif
}  // namespace

TokenEmitter::TokenEmitter(std::ostream& output) : output_(output)
{}

void TokenEmitter::write(const std::string& text, TokenEmissionKind kind)
{
    if (text.empty())
    {
        return;
    }

    if (hasPrevious_ && previousKind_ == TokenEmissionKind::Lexical && kind == TokenEmissionKind::Lexical && requiresSeparator(previousText_, text))
    {
        output_ << ' ';
    }

    output_ << text;
    previousText_ = text;
    previousKind_ = kind;
    hasPrevious_  = true;
}

bool TokenEmitter::good() const
{
    return output_.good();
}
