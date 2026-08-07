#include "TextHandler.hpp"
#include "AtomicFileWriter.hpp"
#include "IdentifierResolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

int debug = 0;

extern int yylineno;

// These flags are shared with the generated lexer/parser. They retain the
// obfuscation state introduced on dev-obfuscator while the file handling and
// diagnostics below use the safer C++ implementation from main.
int if_processing = 0;
int if_id = 0;
int if_type = 0;

namespace
{
enum class ExitCode
{
    Success = 0,
    SyntaxError = 1,
    UsageError = 2,
    InputError = 3,
};

enum class ReadLineResult
{
    Line,
    EndOfFile,
    Error,
};

std::ifstream inputFile;
std::string lineBuffer;

bool endOfFile = false;
bool inputReadError = false;
bool syntaxError = false;

int currentRow = 0;
std::size_t bufferOffset = 0;
std::size_t tokenStart = 0;
std::size_t tokenLength = 0;
std::size_t nextTokenStart = 0;

ReadLineResult getNextLine();

struct ObfuscationToken
{
    std::string text;
    bool isIdentifier = false;
    std::size_t sourceIndex = std::numeric_limits<std::size_t>::max();
};

class BracesQueue
{
public:
    void push()
    {
        ++braces_;
    }

    int pop()
    {
        if (braces_ == 1)
        {
            --braces_;
            return 0;
        }
        if (braces_ > 1)
        {
            --braces_;
            return 1;
        }
        return -1;
    }

private:
    std::size_t braces_ = 0;
};

class Obfuscator
{
public:
    void processToken(const char* token, bool isIdentifier)
    {
        const std::string tokenText(token);
        const std::size_t sourceIndex = sourceTokens_.size();
        sourceTokens_.push_back({tokenText, isIdentifier});
        const ObfuscationToken obfuscationToken{
            tokenText, isIdentifier, sourceIndex};

        if (if_processing != 0)
        {
            tempTokens_.push_back(obfuscationToken);

            if (ifBodyStart_ == 0 && tokenText == "{")
            {
                ifBodyStart_ = tempTokens_.size();
            }

            if (tokenText == "{")
            {
                braces_.push();
            }
            else if (tokenText == "}" && braces_.pop() == 0)
            {
                if_processing = 0;
            }
        }

        if (!tempTokens_.empty() && if_processing == 0)
        {
            appendTokens(
                bodyTokens_, tempTokens_, ifBodyStart_,
                tempTokens_.size() - ifBodyStart_ - 1);

            appendTokens(outputTokens_, tempTokens_, 0, ifBodyStart_);
            insertOpaqueFalseBranch();
            appendTokens(outputTokens_, bodyTokens_, 0, bodyTokens_.size());
            pushOutputToken("}");

            tempTokens_.clear();
            bodyTokens_.clear();
            ifBodyStart_ = 0;
        }
        else if (tempTokens_.empty())
        {
            outputTokens_.push_back(obfuscationToken);
        }
    }

    bool writeResult(std::ostream& output)
    {
        obfuscateIdentifiers();
        obfuscatePunctuators();

        const ObfuscationToken* previous = nullptr;
        for (const ObfuscationToken& token : outputTokens_)
        {
            if (previous != nullptr && needsIdentifierSeparator(
                    *previous, token))
            {
                output << ' ';
            }
            output << token.text;
            previous = &token;
        }
        return output.good();
    }

private:
    std::vector<ObfuscationToken> tempTokens_;
    std::vector<ObfuscationToken> bodyTokens_;
    std::vector<ObfuscationToken> outputTokens_;
    std::vector<IdentifierSourceToken> sourceTokens_;
    BracesQueue braces_;

    std::size_t ifBodyStart_ = 0;
    std::uint32_t opaquePredicateIndex_ = 0;

    static bool isIdentifierCharacter(char character)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_';
    }

    static bool needsIdentifierSeparator(
        const ObfuscationToken& previous,
        const ObfuscationToken& current)
    {
        if (previous.text.empty() || current.text.empty() ||
            (!previous.isIdentifier && !current.isIdentifier))
        {
            return false;
        }
        return isIdentifierCharacter(previous.text.back()) &&
            isIdentifierCharacter(current.text.front());
    }

    static void appendTokens(
        std::vector<ObfuscationToken>& destination,
        const std::vector<ObfuscationToken>& source,
        std::size_t start,
        std::size_t count)
    {
        for (std::size_t index = start; index < start + count; ++index)
        {
            destination.push_back(source[index]);
        }
    }

    void pushOutputToken(const std::string& text)
    {
        outputTokens_.push_back(
            {text, false, std::numeric_limits<std::size_t>::max()});
    }

    static std::string unsignedLiteral(std::uint32_t value)
    {
        std::ostringstream stream;
        stream << "0x" << std::hex << value << 'u';
        return stream.str();
    }

    void insertOpaqueFalseBranch()
    {
        // n * (n + 1) is even for every unsigned n, including after modular
        // overflow. The predicate is therefore always false and does not read
        // or evaluate anything from the user's original condition.
        const std::uint32_t seed =
            0x9e3779b9u ^ (opaquePredicateIndex_ * 0x85ebca6bu);
        const std::uint32_t payloadLeft = seed ^ 0xa5a5a5a5u;
        const std::uint32_t payloadRight = seed ^ 0x5a5a5a5au;
        ++opaquePredicateIndex_;

        const std::string seedLiteral = unsignedLiteral(seed);
        pushOutputToken("if");
        pushOutputToken("(");
        pushOutputToken("(");
        pushOutputToken("(");
        pushOutputToken(seedLiteral);
        pushOutputToken("*");
        pushOutputToken("(");
        pushOutputToken(seedLiteral);
        pushOutputToken("+");
        pushOutputToken("1u");
        pushOutputToken(")");
        pushOutputToken(")");
        pushOutputToken("&");
        pushOutputToken("1u");
        pushOutputToken(")");
        pushOutputToken("!=");
        pushOutputToken("0u");
        pushOutputToken(")");
        pushOutputToken("{");
        pushOutputToken("(");
        pushOutputToken("void");
        pushOutputToken(")");
        pushOutputToken("(");
        pushOutputToken(unsignedLiteral(payloadLeft));
        pushOutputToken("^");
        pushOutputToken(unsignedLiteral(payloadRight));
        pushOutputToken(")");
        pushOutputToken(";");
        pushOutputToken("}");
    }

    void obfuscateIdentifiers()
    {
        const std::vector<std::string> replacements =
            ResolveIdentifierNames(sourceTokens_);

        for (ObfuscationToken& token : outputTokens_)
        {
            if (!token.isIdentifier || token.sourceIndex >= replacements.size())
            {
                continue;
            }
            if (!replacements[token.sourceIndex].empty())
            {
                token.text = replacements[token.sourceIndex];
            }
        }
    }

    void obfuscatePunctuators()
    {
        for (std::size_t index = 0; index < outputTokens_.size(); ++index)
        {
            std::string& token = outputTokens_[index].text;
            if (token == "[")
            {
                token = index % 2 == 0 ? "<:" : "?" "?(";
            }
            else if (token == "]")
            {
                token = index % 2 == 0 ? ":>" : "?" "?)";
            }
            else if (token == "{")
            {
                token = index % 2 == 0 ? "<%" : "?" "?<";
            }
            else if (token == "}")
            {
                token = index % 2 == 0 ? "%>" : "?" "?>";
            }
            else if (token == "#")
            {
                token = index % 2 == 0 ? "%:" : "?" "?=";
            }
            else if (token == "\\")
            {
                token = "?" "?/";
            }
            else if (token == "^")
            {
                token = "?" "?'";
            }
            else if (token == "|")
            {
                token = "?" "?!";
            }
            else if (token == "~")
            {
                token = "?" "?-";
            }
        }
    }
};

Obfuscator obfuscator;

char dumpChar(char character)
{
    const auto value = static_cast<unsigned char>(character);
    return std::isprint(value) != 0 ? character : '@';
}

std::string dumpString(const char* text)
{
    constexpr std::size_t maxLength = 100;
    const std::size_t length = std::min(std::strlen(text), maxLength);

    std::string result;
    result.reserve(length);
    for (std::size_t index = 0; index < length; ++index)
    {
        result.push_back(dumpChar(text[index]));
    }
    return result;
}

int toLocationValue(std::size_t value)
{
    const auto maxValue = static_cast<std::size_t>(std::numeric_limits<int>::max());
    return value > maxValue ? std::numeric_limits<int>::max()
                            : static_cast<int>(value);
}

ReadLineResult getNextLine()
{
    bufferOffset = 0;
    tokenStart = 0;
    nextTokenStart = 1;
    endOfFile = false;
    lineBuffer.clear();

    if (std::getline(inputFile, lineBuffer))
    {
        if (!inputFile.eof())
        {
            lineBuffer.push_back('\n');
        }

        ++currentRow;
        return ReadLineResult::Line;
    }

    if (inputFile.bad() || !inputFile.eof())
    {
        inputReadError = true;
        return ReadLineResult::Error;
    }

    endOfFile = true;
    return ReadLineResult::EndOfFile;
}
} // namespace

int ClassifyPreprocessorDirective(const char* directive)
{
    if (directive == nullptr)
    {
        return PREPROCESSOR_DIRECTIVE;
    }

    const char* cursor = directive;
    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }
    if (*cursor != '#')
    {
        return PREPROCESSOR_DIRECTIVE;
    }

    ++cursor;
    while (*cursor == ' ' || *cursor == '\t')
    {
        ++cursor;
    }

    const char* keywordStart = cursor;
    while (std::isalpha(static_cast<unsigned char>(*cursor)) != 0)
    {
        ++cursor;
    }
    const std::string keyword(keywordStart, cursor);

    if (keyword == "if")
    {
        return PREPROCESSOR_IF;
    }
    if (keyword == "ifdef")
    {
        return PREPROCESSOR_IFDEF;
    }
    if (keyword == "ifndef")
    {
        return PREPROCESSOR_IFNDEF;
    }
    if (keyword == "elif")
    {
        return PREPROCESSOR_ELIF;
    }
    if (keyword == "else")
    {
        return PREPROCESSOR_ELSE;
    }
    if (keyword == "endif")
    {
        return PREPROCESSOR_ENDIF;
    }
    return PREPROCESSOR_DIRECTIVE;
}

void PrintError(const char* message)
{
    syntaxError = true;
    const std::size_t markerLength = std::max<std::size_t>(tokenLength, 1);

    std::cerr << "...... !";
    if (endOfFile)
    {
        std::cerr << std::string(lineBuffer.size(), '.') << "^-EOF\n";
    }
    else
    {
        if (tokenStart != 1)
        {
            std::cerr << std::string(tokenStart, '.');
        }
        std::cerr << std::string(markerLength, '^') << '\n';
    }

    std::cerr << "Error: " << (message != nullptr ? message : "syntax error")
              << " at line " << yylineno << "\n\n";
}

void DumpRow()
{
    if (!syntaxError)
    {
        std::cerr << "\nError(s) occurred while parsing:\n\n";
    }

    std::cerr << std::setw(6) << currentRow << " |" << lineBuffer;
    if (lineBuffer.empty() || lineBuffer.back() != '\n')
    {
        std::cerr << '\n';
    }
}

void BeginToken(const char* token)
{
    if (token == nullptr)
    {
        return;
    }

    const bool isIdentifier = if_id != 0;
    if_id = 0;

    obfuscator.processToken(token, isIdentifier);

    tokenStart = nextTokenStart;
    tokenLength = std::strlen(token);
    nextTokenStart = bufferOffset;

    yylloc.first_line = currentRow;
    yylloc.first_column = toLocationValue(tokenStart);
    yylloc.last_line = currentRow;
    yylloc.last_column = toLocationValue(
        tokenStart + (tokenLength == 0 ? 0 : tokenLength - 1));

    if (debug != 0)
    {
        std::cout << "Token '" << dumpString(token) << "' at "
                  << yylloc.first_column << ':' << yylloc.last_column
                  << " next at " << nextTokenStart << '\n';
    }
}

int GetNextChar(char* destination, int maxBuffer)
{
    if (destination == nullptr || maxBuffer <= 0 || endOfFile)
    {
        return 0;
    }

    while (bufferOffset >= lineBuffer.size())
    {
        if (getNextLine() != ReadLineResult::Line)
        {
            return 0;
        }
    }

    destination[0] = lineBuffer[bufferOffset++];

    if (debug != 0)
    {
        const auto byteValue = static_cast<unsigned int>(
            static_cast<unsigned char>(destination[0]));
        std::cout << "GetNextChar() => '" << dumpChar(destination[0])
                  << "' 0x" << std::hex << byteValue << std::dec
                  << " at " << bufferOffset << '\n';
    }

    return destination[0] == '\0' ? 0 : 1;
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3)
    {
        const char* programName = argc > 0 && argv[0] != nullptr
                                      ? argv[0]
                                      : "OpenSLex";
        std::cerr << "Usage: " << programName
                  << " <input-file> [output-file]\n";
        return static_cast<int>(ExitCode::UsageError);
    }

    const char* inputPath = argv[1];
    const char* outputPath = argc == 3 ? argv[2] : "obfuscated_result.cl";

    inputFile.open(inputPath);
    if (!inputFile.is_open())
    {
        std::cerr << "Error: cannot open input file '" << inputPath << "'.\n";
        return static_cast<int>(ExitCode::InputError);
    }

    const ReadLineResult firstLine = getNextLine();
    if (firstLine == ReadLineResult::Line)
    {
        if (yyparse() != 0)
        {
            syntaxError = true;
        }
    }

    if (firstLine == ReadLineResult::Error || inputReadError)
    {
        std::cerr << "Error: failed to read input file '" << inputPath << "'.\n";
        return static_cast<int>(ExitCode::InputError);
    }

    if (syntaxError)
    {
        return static_cast<int>(ExitCode::SyntaxError);
    }

    inputFile.close();

    std::ostringstream outputBuffer;
    if (!obfuscator.writeResult(outputBuffer))
    {
        std::cerr << "Error: failed to prepare output file '" << outputPath
                  << "'.\n";
        return static_cast<int>(ExitCode::InputError);
    }

    std::string outputError;
    if (!WriteFileAtomically(outputPath, outputBuffer.str(), outputError))
    {
        std::cerr << "Error: failed to write output file '" << outputPath
                  << "': " << outputError << ".\n";
        return static_cast<int>(ExitCode::InputError);
    }

    std::cout << "PASS\n";
    return static_cast<int>(ExitCode::Success);
}
