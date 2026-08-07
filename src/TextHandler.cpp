#include "TextHandler.hpp"
#include "AtomicFileWriter.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

int debug = 0;

extern int yylineno;

// These flags are shared with the generated lexer/parser. They retain the
// obfuscation state introduced on dev-obfuscator while the file handling and
// diagnostics below use the safer C++ implementation from main.
int directives_ended = 0;
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

constexpr std::size_t minGeneratedNameLength = 7;
constexpr std::size_t maxGeneratedNameLength = 1024;

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

std::string lastIdentifier;

ReadLineResult getNextLine();

struct ObfuscationToken
{
    std::string text;
    bool isIdentifier = false;
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
        const ObfuscationToken obfuscationToken{tokenText, isIdentifier};
        bool firstExpressionParsing = false;

        if (if_processing != 0)
        {
            tempTokens_.push_back(obfuscationToken);
            expressionTokens_.push_back(obfuscationToken);

            if (ifExpressionStart_ == 0 && tokenText == "(")
            {
                ifExpressionStart_ = tempTokens_.size();
                ifExpressionParenthesisDepth_ = 1;
            }
            else if (ifExpressionStart_ != 0 && ifExpressionEnd_ == 0)
            {
                if (tokenText == "(")
                {
                    ++ifExpressionParenthesisDepth_;
                }
                else if (tokenText == ")" &&
                         ifExpressionParenthesisDepth_ > 0)
                {
                    --ifExpressionParenthesisDepth_;
                    if (ifExpressionParenthesisDepth_ == 0)
                    {
                        ifExpressionEnd_ = tempTokens_.size() - 1;
                    }
                }
            }

            if (ifBodyStart_ == 0 && tokenText == "{")
            {
                ifBodyStart_ = tempTokens_.size();
            }

            firstExpressionParsing =
                tempTokens_.size() != ifExpressionStart_ &&
                ifExpressionStart_ != 0 && ifExpressionEnd_ == 0;

            if (firstExpressionParsing)
            {
                firstExpressionTokens_.push_back(obfuscationToken);
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
            makeDeadEnd();
            appendTokens(outputTokens_, bodyTokens_, 0, bodyTokens_.size());
            pushOutputToken("}");

            tempTokens_.clear();
            expressionTokens_.clear();
            bodyTokens_.clear();
            firstExpressionTokens_.clear();
            ifExpressionStart_ = 0;
            ifExpressionEnd_ = 0;
            ifBodyStart_ = 0;
            ifExpressionParenthesisDepth_ = 0;
        }
        else if (tempTokens_.empty())
        {
            outputTokens_.push_back(obfuscationToken);
        }
    }

    void rememberIdentifier(const std::string& identifier)
    {
        identifiers_.insert({identifier, 1});
    }

    bool writeResult(std::ostream& output)
    {
        obfuscateIdentifiers();
        obfuscatePunctuators();

        for (const ObfuscationToken& token : outputTokens_)
        {
            output << token.text;
        }
        return output.good();
    }

private:
    std::vector<ObfuscationToken> firstExpressionTokens_;
    std::vector<ObfuscationToken> expressionTokens_;
    std::vector<ObfuscationToken> tempTokens_;
    std::vector<ObfuscationToken> bodyTokens_;
    std::vector<ObfuscationToken> outputTokens_;
    std::map<std::string, int> identifiers_;
    std::map<std::string, std::string> renamedIdentifiers_;
    std::set<std::string> generatedNames_;
    BracesQueue braces_;

    std::size_t ifBodyStart_ = 0;
    std::size_t ifExpressionStart_ = 0;
    std::size_t ifExpressionEnd_ = 0;
    std::size_t ifExpressionParenthesisDepth_ = 0;

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
        outputTokens_.push_back({text, false});
    }

    void makeDeadEnd()
    {
        appendTokens(outputTokens_, tempTokens_, 0, ifExpressionStart_);
        pushOutputToken("!(");
        appendTokens(
            outputTokens_, firstExpressionTokens_, 0,
            firstExpressionTokens_.size());
        pushOutputToken("))");
        pushOutputToken("{");
        obfuscateFullBlock();
        pushOutputToken("}");
    }

    void obfuscateFullBlock()
    {
        for (const ObfuscationToken& token : expressionTokens_)
        {
            if (token.text == "||")
            {
                pushOutputToken("&&");
            }
            else if (token.text == "&&")
            {
                pushOutputToken("||");
            }
            else if (token.text == "|")
            {
                pushOutputToken("^");
            }
            else if (token.text == "&")
            {
                pushOutputToken("|");
            }
            else if (token.text == "+")
            {
                pushOutputToken("*");
            }
            else if (token.text == "-")
            {
                pushOutputToken("+");
            }
            else if (token.text == "%")
            {
                pushOutputToken("/");
            }
            else if (token.text == ">=")
            {
                pushOutputToken("<");
            }
            else if (token.text == "<=")
            {
                pushOutputToken(">");
            }
            else if (token.text == "<")
            {
                pushOutputToken(">=");
            }
            else if (token.text == ">")
            {
                pushOutputToken("<=");
            }
            else if (token.text == "++")
            {
                pushOutputToken("--");
            }
            else if (token.text == "--")
            {
                pushOutputToken("++");
            }
            else
            {
                outputTokens_.push_back(token);
            }
        }
    }

    std::string makeRandomName(std::size_t length) const
    {
        static const char alphanumeric[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static const char alphabetic[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

        std::string result;
        result.reserve(length);
        for (std::size_t index = 0; index < length; ++index)
        {
            const char* alphabet = index == 0 ? alphabetic : alphanumeric;
            const std::size_t alphabetSize =
                index == 0 ? sizeof(alphabetic) - 1 : sizeof(alphanumeric) - 1;
            result.push_back(alphabet[std::rand() % alphabetSize]);
        }
        return result;
    }

    static bool isVectorSelector(const std::string& identifier)
    {
        if (identifier.empty())
        {
            return false;
        }

        const bool xyzw = identifier.size() <= 4 &&
            identifier.find_first_not_of("xyzw") == std::string::npos;
        const bool rgba = identifier.size() <= 4 &&
            identifier.find_first_not_of("rgba") == std::string::npos;
        const bool hexadecimalSelector = identifier.size() >= 2 &&
            identifier.size() <= 17 && identifier.front() == 's' &&
            identifier.find_first_not_of("0123456789abcdefABCDEF", 1) ==
                std::string::npos;
        const bool halfSelector = identifier == "lo" || identifier == "hi" ||
            identifier == "even" || identifier == "odd";
        return xyzw || rgba || hexadecimalSelector || halfSelector;
    }

    static bool isProtectedOpenCLIdentifier(const std::string& identifier)
    {
        if (isVectorSelector(identifier) ||
            identifier.compare(0, 2, "__") == 0 ||
            (identifier.size() >= 2 &&
             identifier.compare(identifier.size() - 2, 2, "_t") == 0))
        {
            return true;
        }

        return std::isupper(static_cast<unsigned char>(identifier.front())) != 0 &&
            identifier.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") ==
                std::string::npos;
    }

    void obfuscateIdentifiers()
    {
        for (const auto& identifier : identifiers_)
        {
            if (isProtectedOpenCLIdentifier(identifier.first))
            {
                continue;
            }

            std::string newName;
            const std::size_t generatedLength = std::max<std::size_t>(
                1, (minGeneratedNameLength + identifier.first.size()) %
                       maxGeneratedNameLength);

            do
            {
                newName = makeRandomName(generatedLength);
            } while (
                identifiers_.find(newName) != identifiers_.end() ||
                generatedNames_.find(newName) != generatedNames_.end());

            renamedIdentifiers_.insert({identifier.first, newName});
            generatedNames_.insert(newName);
        }

        for (ObfuscationToken& token : outputTokens_)
        {
            if (!token.isIdentifier)
            {
                continue;
            }

            const auto renamed = renamedIdentifiers_.find(token.text);
            if (renamed != renamedIdentifiers_.end())
            {
                token.text = renamed->second;
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

    const std::string currentToken(token);
    const bool isIdentifier = if_id != 0;
    if (isIdentifier)
    {
        lastIdentifier = currentToken;
        if_id = 0;
    }
    else if (currentToken != "(" && !lastIdentifier.empty())
    {
        obfuscator.rememberIdentifier(lastIdentifier);
        lastIdentifier.clear();
    }
    else
    {
        lastIdentifier.clear();
    }

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
