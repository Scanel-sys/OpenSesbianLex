#include "TextHandler.hpp"
#include "AtomicFileWriter.hpp"
#include "ClangFrontend.hpp"
#include "IdentifierResolver.hpp"

#include <algorithm>
#include <cerrno>
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
    FrontendError = 4,
};

enum class FrontendMode
{
    Auto,
    Clang,
    Legacy,
};

enum class ReadLineResult
{
    Line,
    EndOfFile,
    Error,
};

std::ifstream inputFile;
std::istringstream transformedInput;
std::istream* activeInput = &inputFile;
std::string lineBuffer;

bool endOfFile = false;
bool inputReadError = false;
bool syntaxError = false;
bool suppressDiagnostics = false;
bool legacyOpaquePredicatePass = true;

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
    void setSeed(std::uint32_t seed)
    {
        seed_ = seed == 0 ? 0x6d2b79f5u : seed;
    }

    void setIdentifiersAlreadyResolved(bool value)
    {
        identifiersAlreadyResolved_ = value;
    }

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
        if (!identifiersAlreadyResolved_)
        {
            obfuscateIdentifiers();
        }
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
    std::uint32_t seed_ = 0x9e3779b9u;
    bool identifiersAlreadyResolved_ = false;

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
            seed_ ^ (opaquePredicateIndex_ * 0x85ebca6bu);
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
            ResolveIdentifierNames(sourceTokens_, seed_);

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

    if (std::getline(*activeInput, lineBuffer))
    {
        if (!activeInput->eof())
        {
            lineBuffer.push_back('\n');
        }

        ++currentRow;
        return ReadLineResult::Line;
    }

    if (activeInput->bad() || !activeInput->eof())
    {
        inputReadError = true;
        return ReadLineResult::Error;
    }

    endOfFile = true;
    return ReadLineResult::EndOfFile;
}

bool parseSeed(const char* text, std::uint32_t& seed)
{
    if (text == nullptr || *text == '\0' || *text == '-')
    {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    seed = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parseFrontendMode(const char* text, FrontendMode& mode)
{
    if (text == nullptr)
    {
        return false;
    }
    if (std::strcmp(text, "auto") == 0)
    {
        mode = FrontendMode::Auto;
        return true;
    }
    if (std::strcmp(text, "clang") == 0)
    {
        mode = FrontendMode::Clang;
        return true;
    }
    if (std::strcmp(text, "legacy") == 0)
    {
        mode = FrontendMode::Legacy;
        return true;
    }
    return false;
}

bool hasOpenCLFileExtension(const std::string& path)
{
    if (path.size() < 3)
    {
        return false;
    }
    std::string extension = path.substr(path.size() - 3);
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](char character) {
            return static_cast<char>(std::tolower(
                static_cast<unsigned char>(character)));
        });
    return extension == ".cl";
}

bool readSourceFile(
    const char* path,
    std::string& source,
    std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        error = "cannot open input file";
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        error = "failed to read input file";
        return false;
    }
    source = buffer.str();
    return true;
}

void printUsage(const char* programName)
{
    std::cerr << "Usage: " << programName
              << " [--seed <uint32>] [--frontend auto|clang|legacy]"
                 " [--clang-arg <argument>]..."
                 " <input-file> [output-file]\n";
}
} // namespace

bool UseLegacyOpaquePredicatePass()
{
    return legacyOpaquePredicatePass;
}

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
    if (suppressDiagnostics)
    {
        return;
    }
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
    if (suppressDiagnostics)
    {
        return;
    }
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

#ifdef OPEN_SLEX_NO_MAIN
void ResetOpenSLexFrontendForFuzzing()
{
    if (inputFile.is_open())
    {
        inputFile.close();
    }
    inputFile.clear();
    transformedInput.clear();
    transformedInput.str("");
    activeInput = &inputFile;
    lineBuffer.clear();
    endOfFile = false;
    inputReadError = false;
    syntaxError = false;
    suppressDiagnostics = true;
    legacyOpaquePredicatePass = true;
    currentRow = 1;
    bufferOffset = 0;
    tokenStart = 0;
    tokenLength = 0;
    nextTokenStart = 0;
    if_processing = 0;
    if_id = 0;
    if_type = 0;
    yylineno = 1;
    obfuscator = Obfuscator{};
}
#else
int main(int argc, char* argv[])
{
    const char* programName = argc > 0 && argv[0] != nullptr
                                  ? argv[0]
                                  : "OpenSLex";
    int argumentIndex = 1;
    std::uint32_t seed = 0x9e3779b9u;
    FrontendMode frontendMode = FrontendMode::Auto;
    std::vector<std::string> clangArguments;

    while (argumentIndex < argc)
    {
        if (std::strcmp(argv[argumentIndex], "--seed") == 0)
        {
            if (argumentIndex + 1 >= argc ||
                !parseSeed(argv[argumentIndex + 1], seed))
            {
                std::cerr
                    << "Error: --seed requires an unsigned 32-bit value.\n";
                printUsage(programName);
                return static_cast<int>(ExitCode::UsageError);
            }
            argumentIndex += 2;
            continue;
        }
        if (std::strcmp(argv[argumentIndex], "--frontend") == 0)
        {
            if (argumentIndex + 1 >= argc ||
                !parseFrontendMode(argv[argumentIndex + 1], frontendMode))
            {
                std::cerr
                    << "Error: --frontend requires auto, clang, or legacy.\n";
                printUsage(programName);
                return static_cast<int>(ExitCode::UsageError);
            }
            argumentIndex += 2;
            continue;
        }
        if (std::strcmp(argv[argumentIndex], "--clang-arg") == 0)
        {
            if (argumentIndex + 1 >= argc)
            {
                std::cerr << "Error: --clang-arg requires an argument.\n";
                printUsage(programName);
                return static_cast<int>(ExitCode::UsageError);
            }
            clangArguments.push_back(argv[argumentIndex + 1]);
            argumentIndex += 2;
            continue;
        }
        const char clangArgumentPrefix[] = "--clang-arg=";
        if (std::strncmp(
                argv[argumentIndex],
                clangArgumentPrefix,
                sizeof(clangArgumentPrefix) - 1) == 0)
        {
            clangArguments.push_back(
                argv[argumentIndex] + sizeof(clangArgumentPrefix) - 1);
            ++argumentIndex;
            continue;
        }
        break;
    }

    const int positionalCount = argc - argumentIndex;
    if (positionalCount < 1 || positionalCount > 2)
    {
        printUsage(programName);
        return static_cast<int>(ExitCode::UsageError);
    }

    const char* inputPath = argv[argumentIndex];
    const char* outputPath = positionalCount == 2
        ? argv[argumentIndex + 1]
        : "obfuscated_result.cl";
    obfuscator.setSeed(seed);

    const bool useClangFrontend =
        frontendMode == FrontendMode::Clang ||
        (frontendMode == FrontendMode::Auto &&
         HasClangSemanticFrontend() &&
         hasOpenCLFileExtension(inputPath));

    if (frontendMode == FrontendMode::Clang &&
        !HasClangSemanticFrontend())
    {
        std::cerr
            << "Error: this OpenSLex build does not contain the Clang "
               "semantic frontend. Reconfigure with "
               "-DOPEN_SLEX_FRONTEND=CLANG.\n";
        return static_cast<int>(ExitCode::FrontendError);
    }

    if (useClangFrontend)
    {
        std::string source;
        std::string inputError;
        if (!readSourceFile(inputPath, source, inputError))
        {
            std::cerr << "Error: " << inputError << " '" << inputPath
                      << "'.\n";
            return static_cast<int>(ExitCode::InputError);
        }

        ClangFrontendOptions options;
        options.seed = seed;
        options.compilerArguments = clangArguments;
        const ClangFrontendResult frontend = RunClangSemanticFrontend(
            inputPath, source, options);
        if (!frontend.diagnostics.empty())
        {
            std::cerr << frontend.diagnostics;
            if (frontend.diagnostics.back() != '\n')
            {
                std::cerr << '\n';
            }
        }
        if (frontend.status != ClangFrontendStatus::Success)
        {
            return static_cast<int>(
                frontend.status == ClangFrontendStatus::SyntaxError
                    ? ExitCode::SyntaxError
                    : ExitCode::FrontendError);
        }

        transformedInput.str(frontend.transformedSource);
        transformedInput.clear();
        activeInput = &transformedInput;
        legacyOpaquePredicatePass = false;
        obfuscator.setIdentifiersAlreadyResolved(true);
    }
    else
    {
        inputFile.open(inputPath);
        if (!inputFile.is_open())
        {
            std::cerr << "Error: cannot open input file '" << inputPath
                      << "'.\n";
            return static_cast<int>(ExitCode::InputError);
        }
        activeInput = &inputFile;
    }

    const ReadLineResult firstLine = getNextLine();
    if (firstLine == ReadLineResult::Line)
    {
        if (useClangFrontend)
        {
            while (yylex() != 0)
            {
            }
        }
        else if (yyparse() != 0)
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

    if (inputFile.is_open())
    {
        inputFile.close();
    }

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
#endif
