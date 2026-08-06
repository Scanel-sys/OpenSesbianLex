#include "TextHandler.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

int debug = 0;

extern int yylineno;

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
        // std::getline removes the delimiter. Restore it so Flex sees the
        // same input stream as it did with fgets, except for a final line
        // that genuinely has no newline.
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
    if (argc != 2)
    {
        const char* programName = argc > 0 && argv[0] != nullptr
                                      ? argv[0]
                                      : "OpenSLex";
        std::cerr << "Usage: " << programName << " <input-file>\n";
        return static_cast<int>(ExitCode::UsageError);
    }

    const char* inputPath = argv[1];
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

    std::cout << "PASS\n";
    return static_cast<int>(ExitCode::Success);
}
