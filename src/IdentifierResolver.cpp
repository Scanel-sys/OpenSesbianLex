#include "IdentifierResolver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
const std::size_t noIndex = std::numeric_limits<std::size_t>::max();
const std::size_t minGeneratedNameLength = 7;
const std::size_t maxGeneratedNameLength = 1024;

enum class SymbolKind
{
    Variable,
    Parameter,
    Function,
    TypedefName,
    StructTag,
    Field,
};

enum class TypeKind
{
    Unknown,
    Scalar,
    Vector,
    Structure,
};

struct TypeRef
{
    TypeKind kind = TypeKind::Unknown;
    std::size_t structure = noIndex;
    unsigned int pointerDepth = 0;
};

struct SemanticToken
{
    std::string text;
    bool isIdentifier = false;
    std::size_t sourceIndex = noIndex;
};

struct Symbol
{
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    TypeRef type;
    std::size_t declarationToken = noIndex;
    std::size_t scope = noIndex;
    bool renameable = true;
};

struct Scope
{
    std::size_t parent = noIndex;
    std::map<std::string, std::vector<std::size_t>> names;
    std::map<std::string, std::vector<std::size_t>> tags;
};

struct Structure
{
    std::map<std::string, std::size_t> fields;
    bool fieldsParsed = false;
};

struct ParsedType
{
    bool success = false;
    bool isTypedef = false;
    bool isKernel = false;
    bool definedStructure = false;
    TypeRef type;
    std::size_t next = noIndex;
};

bool containsNewline(const std::string& text)
{
    return text.find('\n') != std::string::npos ||
        text.find('\r') != std::string::npos;
}

bool isWhitespace(const std::string& text)
{
    if (text.empty())
    {
        return true;
    }

    for (char character : text)
    {
        if (std::isspace(static_cast<unsigned char>(character)) == 0)
        {
            return false;
        }
    }
    return true;
}

bool hasSuffix(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class Resolver
{
public:
    explicit Resolver(const std::vector<IdentifierSourceToken>& sourceTokens)
        : sourceTokens_(sourceTokens),
          bindingBySource_(sourceTokens.size(), noIndex),
          protectedSource_(sourceTokens.size(), false)
    {
        for (const IdentifierSourceToken& token : sourceTokens_)
        {
            if (token.isIdentifier)
            {
                originalIdentifiers_.insert(token.text);
            }
            collectPreprocessorReferences(token.text);
        }

        buildSemanticTokens();
        buildMatchingPairs();
        buildForScopeEvents();
    }

    std::vector<std::string> run()
    {
        scopes_.push_back(Scope{});
        scan();
        bindForwardFunctionCalls();

        std::vector<std::string> replacementBySource(sourceTokens_.size());
        std::vector<std::string> replacementBySymbol(symbols_.size());
        for (std::size_t symbolIndex = 0;
             symbolIndex < symbols_.size();
             ++symbolIndex)
        {
            if (!symbols_[symbolIndex].renameable ||
                preprocessorReferences_.find(symbols_[symbolIndex].name) !=
                    preprocessorReferences_.end())
            {
                continue;
            }
            replacementBySymbol[symbolIndex] = makeUniqueName(
                symbols_[symbolIndex].name);
        }

        for (std::size_t sourceIndex = 0;
             sourceIndex < bindingBySource_.size();
             ++sourceIndex)
        {
            const std::size_t symbolIndex = bindingBySource_[sourceIndex];
            if (symbolIndex != noIndex && !protectedSource_[sourceIndex])
            {
                replacementBySource[sourceIndex] =
                    replacementBySymbol[symbolIndex];
            }
        }
        return replacementBySource;
    }

private:
    const std::vector<IdentifierSourceToken>& sourceTokens_;
    std::vector<SemanticToken> tokens_;
    std::vector<std::size_t> bindingBySource_;
    std::vector<bool> protectedSource_;
    std::vector<std::size_t> matching_;
    std::vector<bool> ignored_;
    std::vector<unsigned int> forScopePush_;
    std::vector<unsigned int> forScopePopAfter_;
    std::map<std::size_t, std::vector<std::size_t>> parametersAtBody_;
    std::vector<Scope> scopes_;
    std::vector<Symbol> symbols_;
    std::vector<Structure> structures_;
    std::set<std::string> originalIdentifiers_;
    std::set<std::string> generatedNames_;
    std::set<std::string> preprocessorReferences_;

    static bool isIdentifierStart(char character)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        return std::isalpha(value) != 0 || character == '_';
    }

    static bool isIdentifierContinuation(char character)
    {
        const unsigned char value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_';
    }

    static void skipHorizontalWhitespace(
        const std::string& text,
        std::size_t& cursor)
    {
        while (cursor < text.size() &&
               (text[cursor] == ' ' || text[cursor] == '\t'))
        {
            ++cursor;
        }
    }

    static std::string readIdentifier(
        const std::string& text,
        std::size_t& cursor)
    {
        if (cursor >= text.size() || !isIdentifierStart(text[cursor]))
        {
            return std::string();
        }
        const std::size_t start = cursor++;
        while (cursor < text.size() &&
               isIdentifierContinuation(text[cursor]))
        {
            ++cursor;
        }
        return text.substr(start, cursor - start);
    }

    void collectPreprocessorReferences(const std::string& text)
    {
        std::size_t cursor = 0;
        skipHorizontalWhitespace(text, cursor);
        if (cursor >= text.size() || text[cursor] != '#')
        {
            return;
        }

        ++cursor;
        skipHorizontalWhitespace(text, cursor);
        if (readIdentifier(text, cursor) != "define")
        {
            return;
        }

        skipHorizontalWhitespace(text, cursor);
        const std::string macroName = readIdentifier(text, cursor);
        if (macroName.empty())
        {
            return;
        }

        std::set<std::string> macroParameters;
        if (cursor < text.size() && text[cursor] == '(')
        {
            ++cursor;
            while (cursor < text.size() && text[cursor] != ')')
            {
                if (isIdentifierStart(text[cursor]))
                {
                    macroParameters.insert(readIdentifier(text, cursor));
                }
                else
                {
                    ++cursor;
                }
            }
            if (cursor < text.size())
            {
                ++cursor;
            }
        }

        macroParameters.insert("__VA_ARGS__");
        while (cursor < text.size())
        {
            if (!isIdentifierStart(text[cursor]))
            {
                ++cursor;
                continue;
            }
            const std::string identifier = readIdentifier(text, cursor);
            if (identifier != macroName &&
                macroParameters.find(identifier) == macroParameters.end())
            {
                preprocessorReferences_.insert(identifier);
            }
        }
    }

    void buildSemanticTokens()
    {
        bool inString = false;
        bool inDirective = false;
        std::string lastDirectiveToken;

        for (std::size_t sourceIndex = 0;
             sourceIndex < sourceTokens_.size();
             ++sourceIndex)
        {
            const IdentifierSourceToken& token = sourceTokens_[sourceIndex];

            if (inDirective)
            {
                if (token.isIdentifier)
                {
                    protectedSource_[sourceIndex] = true;
                }

                if (containsNewline(token.text))
                {
                    const bool continued = lastDirectiveToken == "\\";
                    if (!continued)
                    {
                        inDirective = false;
                    }
                    lastDirectiveToken.clear();
                }
                else if (!isWhitespace(token.text))
                {
                    lastDirectiveToken = token.text;
                }
                continue;
            }

            if (!inString && token.text == "#")
            {
                inDirective = true;
                lastDirectiveToken = token.text;
                continue;
            }

            if (token.text == "\"")
            {
                inString = !inString;
                continue;
            }
            if (inString || isWhitespace(token.text))
            {
                continue;
            }

            tokens_.push_back(
                SemanticToken{token.text, token.isIdentifier, sourceIndex});
        }

        ignored_.assign(tokens_.size(), false);
        forScopePush_.assign(tokens_.size(), 0);
        forScopePopAfter_.assign(tokens_.size(), 0);
    }

    void buildMatchingPairs()
    {
        matching_.assign(tokens_.size(), noIndex);
        std::vector<std::size_t> parentheses;
        std::vector<std::size_t> braces;
        std::vector<std::size_t> brackets;

        for (std::size_t index = 0; index < tokens_.size(); ++index)
        {
            const std::string& text = tokens_[index].text;
            std::vector<std::size_t>* stack = nullptr;
            if (text == "(")
            {
                parentheses.push_back(index);
            }
            else if (text == "{")
            {
                braces.push_back(index);
            }
            else if (text == "[")
            {
                brackets.push_back(index);
            }
            else if (text == ")")
            {
                stack = &parentheses;
            }
            else if (text == "}")
            {
                stack = &braces;
            }
            else if (text == "]")
            {
                stack = &brackets;
            }

            if (stack != nullptr && !stack->empty())
            {
                const std::size_t open = stack->back();
                stack->pop_back();
                matching_[open] = index;
                matching_[index] = open;
            }
        }
    }

    void buildForScopeEvents()
    {
        for (std::size_t index = 0; index + 1 < tokens_.size(); ++index)
        {
            if (tokens_[index].text != "for" ||
                tokens_[index + 1].text != "(" ||
                matching_[index + 1] == noIndex)
            {
                continue;
            }

            const std::size_t closeParenthesis = matching_[index + 1];
            if (closeParenthesis + 1 >= tokens_.size())
            {
                continue;
            }

            std::size_t end = closeParenthesis + 1;
            if (tokens_[end].text == "{" && matching_[end] != noIndex)
            {
                end = matching_[end];
            }
            else
            {
                while (end + 1 < tokens_.size() &&
                       tokens_[end].text != ";")
                {
                    ++end;
                }
            }

            ++forScopePush_[index + 1];
            ++forScopePopAfter_[end];
        }
    }

    static bool isTypeQualifier(const std::string& text)
    {
        static const std::set<std::string> qualifiers = {
            "const", "volatile", "restrict", "__global", "global",
            "__local", "local", "__constant", "constant", "__private",
            "private", "read_only", "write_only", "read_write",
            "__read_only", "__write_only", "__read_write", "static",
            "extern", "inline"};
        return qualifiers.find(text) != qualifiers.end();
    }

    static bool isScalarType(const std::string& text)
    {
        static const std::set<std::string> scalarTypes = {
            "void", "bool", "char", "uchar", "short", "ushort", "int",
            "uint", "long", "ulong", "half", "float", "double", "size_t",
            "ptrdiff_t", "intptr_t", "uintptr_t", "unsigned", "signed"};
        return scalarTypes.find(text) != scalarTypes.end();
    }

    static bool isVectorType(const std::string& text)
    {
        static const std::set<std::string> bases = {
            "char", "uchar", "short", "ushort", "int", "uint", "long",
            "ulong", "half", "float", "double"};
        static const std::set<std::string> widths = {
            "2", "3", "4", "8", "16"};

        for (const std::string& width : widths)
        {
            if (hasSuffix(text, width))
            {
                const std::string base = text.substr(0, text.size() - width.size());
                return bases.find(base) != bases.end();
            }
        }
        return false;
    }

    static bool isKnownExternalType(const std::string& text)
    {
        static const std::set<std::string> openClTypes = {
            "atomic_int", "atomic_uint", "atomic_long", "atomic_ulong",
            "atomic_float", "atomic_double", "atomic_flag", "memory_order",
            "memory_scope", "cl_mem_fence_flags"};
        return hasSuffix(text, "_t") ||
            openClTypes.find(text) != openClTypes.end();
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
        const bool hexadecimal = identifier.size() >= 2 &&
            identifier.size() <= 17 && identifier.front() == 's' &&
            identifier.find_first_not_of("0123456789abcdefABCDEF", 1) ==
                std::string::npos;
        const bool halfSelector = identifier == "lo" || identifier == "hi" ||
            identifier == "even" || identifier == "odd";
        return xyzw || rgba || hexadecimal || halfSelector;
    }

    std::size_t createScope(std::size_t parent)
    {
        Scope scope;
        scope.parent = parent;
        scopes_.push_back(scope);
        return scopes_.size() - 1;
    }

    void addSymbolToScope(std::size_t scope, std::size_t symbol)
    {
        if (scope == noIndex)
        {
            return;
        }
        symbols_[symbol].scope = scope;
        scopes_[scope].names[symbols_[symbol].name].push_back(symbol);
    }

    std::size_t createSymbol(
        const std::string& name,
        SymbolKind kind,
        const TypeRef& type,
        std::size_t declarationToken,
        std::size_t scope,
        bool renameable)
    {
        Symbol symbol;
        symbol.name = name;
        symbol.kind = kind;
        symbol.type = type;
        symbol.declarationToken = declarationToken;
        symbol.renameable = renameable;
        symbols_.push_back(symbol);
        const std::size_t result = symbols_.size() - 1;
        addSymbolToScope(scope, result);
        return result;
    }

    std::size_t findExactFunction(std::size_t scope, const std::string& name)
    {
        const auto found = scopes_[scope].names.find(name);
        if (found == scopes_[scope].names.end())
        {
            return noIndex;
        }
        for (std::size_t symbol : found->second)
        {
            if (symbols_[symbol].kind == SymbolKind::Function)
            {
                return symbol;
            }
        }
        return noIndex;
    }

    std::size_t declareFunction(
        std::size_t tokenIndex,
        std::size_t scope,
        const TypeRef& returnType,
        bool kernel)
    {
        const std::string& name = tokens_[tokenIndex].text;
        std::size_t symbol = findExactFunction(scope, name);
        if (symbol == noIndex)
        {
            symbol = createSymbol(
                name, SymbolKind::Function, returnType, tokenIndex, scope,
                !kernel && name != "main");
        }
        else if (kernel || name == "main")
        {
            symbols_[symbol].renameable = false;
        }
        bind(tokenIndex, symbol);
        return symbol;
    }

    std::size_t lookupName(
        std::size_t scope,
        const std::string& name,
        std::size_t useToken,
        bool typedefOnly = false) const
    {
        for (std::size_t current = scope;
             current != noIndex;
             current = scopes_[current].parent)
        {
            const auto found = scopes_[current].names.find(name);
            if (found == scopes_[current].names.end())
            {
                continue;
            }

            for (auto symbol = found->second.rbegin();
                 symbol != found->second.rend();
                 ++symbol)
            {
                const Symbol& candidate = symbols_[*symbol];
                if (candidate.declarationToken <= useToken &&
                    (!typedefOnly ||
                     candidate.kind == SymbolKind::TypedefName))
                {
                    return *symbol;
                }
            }
        }
        return noIndex;
    }

    std::size_t lookupTag(
        std::size_t scope,
        const std::string& name,
        std::size_t useToken) const
    {
        for (std::size_t current = scope;
             current != noIndex;
             current = scopes_[current].parent)
        {
            const auto found = scopes_[current].tags.find(name);
            if (found == scopes_[current].tags.end())
            {
                continue;
            }
            for (auto symbol = found->second.rbegin();
                 symbol != found->second.rend();
                 ++symbol)
            {
                if (symbols_[*symbol].declarationToken <= useToken)
                {
                    return *symbol;
                }
            }
        }
        return noIndex;
    }

    void bind(std::size_t tokenIndex, std::size_t symbol)
    {
        if (tokenIndex == noIndex || tokenIndex >= tokens_.size())
        {
            return;
        }
        bindingBySource_[tokens_[tokenIndex].sourceIndex] = symbol;
    }

    std::size_t binding(std::size_t tokenIndex) const
    {
        if (tokenIndex >= tokens_.size())
        {
            return noIndex;
        }
        return bindingBySource_[tokens_[tokenIndex].sourceIndex];
    }

    std::size_t findOrCreateTag(
        std::size_t tokenIndex,
        std::size_t scope,
        bool definition)
    {
        const std::string& name = tokens_[tokenIndex].text;
        std::size_t tag = lookupTag(scope, name, tokenIndex);
        if (tag == noIndex)
        {
            TypeRef tagType;
            tagType.kind = TypeKind::Structure;
            structures_.push_back(Structure{});
            tagType.structure = structures_.size() - 1;
            tag = createSymbol(
                name, SymbolKind::StructTag, tagType, tokenIndex, noIndex, true);
            symbols_[tag].scope = scope;
            scopes_[scope].tags[name].push_back(tag);
        }
        else if (definition && symbols_[tag].type.structure == noIndex)
        {
            structures_.push_back(Structure{});
            symbols_[tag].type.kind = TypeKind::Structure;
            symbols_[tag].type.structure = structures_.size() - 1;
        }
        bind(tokenIndex, tag);
        return tag;
    }

    bool looksLikeExternalType(std::size_t index, std::size_t end) const
    {
        if (!tokens_[index].isIdentifier ||
            !isKnownExternalType(tokens_[index].text))
        {
            return false;
        }

        std::size_t next = index + 1;
        while (next < end &&
               (tokens_[next].text == "*" ||
                isTypeQualifier(tokens_[next].text)))
        {
            ++next;
        }
        return next < end && tokens_[next].isIdentifier;
    }

    ParsedType parseType(
        std::size_t start,
        std::size_t end,
        std::size_t scope)
    {
        ParsedType parsed;
        parsed.next = start;
        bool sawBaseType = false;

        while (parsed.next < end)
        {
            const std::size_t index = parsed.next;
            const std::string& text = tokens_[index].text;

            if (text == "typedef")
            {
                parsed.isTypedef = true;
                ++parsed.next;
                continue;
            }
            if (text == "__kernel" || text == "kernel")
            {
                parsed.isKernel = true;
                ++parsed.next;
                continue;
            }
            if (isTypeQualifier(text))
            {
                ++parsed.next;
                continue;
            }
            if (isVectorType(text))
            {
                parsed.type.kind = TypeKind::Vector;
                sawBaseType = true;
                ++parsed.next;
                continue;
            }
            if (isScalarType(text))
            {
                if (parsed.type.kind != TypeKind::Vector)
                {
                    parsed.type.kind = TypeKind::Scalar;
                }
                sawBaseType = true;
                ++parsed.next;
                continue;
            }
            if (text == "struct" || text == "union")
            {
                sawBaseType = true;
                ++parsed.next;
                std::size_t structure = noIndex;
                if (parsed.next < end && tokens_[parsed.next].isIdentifier)
                {
                    const std::size_t tagToken = parsed.next;
                    const bool definition = tagToken + 1 < end &&
                        tokens_[tagToken + 1].text == "{";
                    const std::size_t tag = findOrCreateTag(
                        tagToken, scope, definition);
                    structure = symbols_[tag].type.structure;
                    ++parsed.next;
                }

                if (parsed.next < end && tokens_[parsed.next].text == "{" &&
                    matching_[parsed.next] != noIndex)
                {
                    const std::size_t open = parsed.next;
                    const std::size_t close = matching_[open];
                    if (structure == noIndex)
                    {
                        structures_.push_back(Structure{});
                        structure = structures_.size() - 1;
                    }
                    parsed.type.kind = TypeKind::Structure;
                    parsed.type.structure = structure;
                    parsed.definedStructure = true;

                    for (std::size_t ignored = open; ignored <= close; ++ignored)
                    {
                        ignored_[ignored] = true;
                    }
                    if (!structures_[structure].fieldsParsed)
                    {
                        structures_[structure].fieldsParsed = true;
                        parseStructureFields(open + 1, close, structure, scope);
                    }
                    parsed.next = close + 1;
                }
                else
                {
                    parsed.type.kind = TypeKind::Structure;
                    parsed.type.structure = structure;
                }
                continue;
            }
            if (tokens_[index].isIdentifier && !sawBaseType)
            {
                const std::size_t alias = lookupName(
                    scope, text, index, true);
                if (alias != noIndex)
                {
                    bind(index, alias);
                    parsed.type = symbols_[alias].type;
                    sawBaseType = true;
                    ++parsed.next;
                    continue;
                }
                if (looksLikeExternalType(index, end))
                {
                    parsed.type.kind = TypeKind::Unknown;
                    protectedSource_[tokens_[index].sourceIndex] = true;
                    sawBaseType = true;
                    ++parsed.next;
                    continue;
                }
            }
            break;
        }

        parsed.success = sawBaseType;
        return parsed;
    }

    void parseStructureFields(
        std::size_t begin,
        std::size_t end,
        std::size_t structure,
        std::size_t scope)
    {
        std::size_t index = begin;
        while (index < end)
        {
            const ParsedType parsed = parseType(index, end, scope);
            if (!parsed.success)
            {
                ++index;
                continue;
            }

            std::size_t cursor = parsed.next;
            bool declaredAny = false;
            while (cursor < end)
            {
                TypeRef fieldType = parsed.type;
                while (cursor < end &&
                       (tokens_[cursor].text == "*" ||
                        isTypeQualifier(tokens_[cursor].text)))
                {
                    if (tokens_[cursor].text == "*")
                    {
                        ++fieldType.pointerDepth;
                    }
                    ++cursor;
                }
                if (cursor >= end || !tokens_[cursor].isIdentifier)
                {
                    break;
                }

                const std::size_t field = createSymbol(
                    tokens_[cursor].text, SymbolKind::Field, fieldType,
                    cursor, noIndex, true);
                structures_[structure].fields[tokens_[cursor].text] = field;
                bind(cursor, field);
                declaredAny = true;
                ++cursor;

                while (cursor < end && tokens_[cursor].text == "[" &&
                       matching_[cursor] != noIndex)
                {
                    cursor = matching_[cursor] + 1;
                }

                int parentheses = 0;
                int braces = 0;
                int brackets = 0;
                while (cursor < end)
                {
                    const std::string& text = tokens_[cursor].text;
                    if (text == "(")
                    {
                        ++parentheses;
                    }
                    else if (text == ")")
                    {
                        --parentheses;
                    }
                    else if (text == "{")
                    {
                        ++braces;
                    }
                    else if (text == "}")
                    {
                        --braces;
                    }
                    else if (text == "[")
                    {
                        ++brackets;
                    }
                    else if (text == "]")
                    {
                        --brackets;
                    }
                    else if (parentheses == 0 && braces == 0 && brackets == 0 &&
                             (text == "," || text == ";"))
                    {
                        break;
                    }
                    ++cursor;
                }

                if (cursor >= end || tokens_[cursor].text == ";")
                {
                    break;
                }
                ++cursor;
            }

            while (index < end && tokens_[index].text != ";")
            {
                if (tokens_[index].text == "{" && matching_[index] != noIndex)
                {
                    index = matching_[index];
                }
                ++index;
            }
            if (index < end)
            {
                ++index;
            }
            if (!declaredAny && parsed.next <= begin)
            {
                ++index;
            }
        }
    }

    std::vector<std::size_t> parseParameters(
        std::size_t begin,
        std::size_t end,
        std::size_t declarationScope)
    {
        std::vector<std::size_t> parameters;
        std::size_t segmentStart = begin;

        while (segmentStart < end)
        {
            std::size_t segmentEnd = segmentStart;
            int parentheses = 0;
            int brackets = 0;
            while (segmentEnd < end)
            {
                const std::string& text = tokens_[segmentEnd].text;
                if (text == "(")
                {
                    ++parentheses;
                }
                else if (text == ")")
                {
                    --parentheses;
                }
                else if (text == "[")
                {
                    ++brackets;
                }
                else if (text == "]")
                {
                    --brackets;
                }
                else if (text == "," && parentheses == 0 && brackets == 0)
                {
                    break;
                }
                ++segmentEnd;
            }

            ParsedType parsed = parseType(
                segmentStart, segmentEnd, declarationScope);
            if (parsed.success)
            {
                TypeRef parameterType = parsed.type;
                std::size_t cursor = parsed.next;
                while (cursor < segmentEnd &&
                       (tokens_[cursor].text == "*" ||
                        isTypeQualifier(tokens_[cursor].text)))
                {
                    if (tokens_[cursor].text == "*")
                    {
                        ++parameterType.pointerDepth;
                    }
                    ++cursor;
                }
                if (cursor < segmentEnd && tokens_[cursor].isIdentifier)
                {
                    const std::size_t parameter = createSymbol(
                        tokens_[cursor].text, SymbolKind::Parameter,
                        parameterType, cursor, noIndex, true);
                    bind(cursor, parameter);
                    parameters.push_back(parameter);
                }
            }

            segmentStart = segmentEnd + 1;
        }
        return parameters;
    }

    bool tryParseDeclaration(
        std::size_t start,
        std::size_t scope,
        std::size_t& consumedEnd)
    {
        ParsedType parsed = parseType(start, tokens_.size(), scope);
        if (!parsed.success)
        {
            return false;
        }

        std::size_t cursor = parsed.next;
        if (cursor < tokens_.size() && tokens_[cursor].text == ";" &&
            parsed.definedStructure)
        {
            consumedEnd = cursor;
            return true;
        }

        bool declaredAny = false;
        while (cursor < tokens_.size())
        {
            TypeRef declaredType = parsed.type;
            while (cursor < tokens_.size() &&
                   (tokens_[cursor].text == "*" ||
                    isTypeQualifier(tokens_[cursor].text)))
            {
                if (tokens_[cursor].text == "*")
                {
                    ++declaredType.pointerDepth;
                }
                ++cursor;
            }

            if (cursor >= tokens_.size() || !tokens_[cursor].isIdentifier)
            {
                return false;
            }

            const std::size_t declarationToken = cursor;
            const std::string name = tokens_[cursor].text;
            ++cursor;

            if (!parsed.isTypedef && cursor < tokens_.size() &&
                tokens_[cursor].text == "(" && matching_[cursor] != noIndex)
            {
                declareFunction(
                    declarationToken, scope, declaredType, parsed.isKernel);
                const std::size_t closeParenthesis = matching_[cursor];
                const std::vector<std::size_t> parameters = parseParameters(
                    cursor + 1, closeParenthesis, scope);
                const std::size_t after = closeParenthesis + 1;
                if (after < tokens_.size() && tokens_[after].text == "{")
                {
                    parametersAtBody_[after] = parameters;
                    consumedEnd = closeParenthesis;
                }
                else if (after < tokens_.size() && tokens_[after].text == ";")
                {
                    consumedEnd = after;
                }
                else
                {
                    consumedEnd = closeParenthesis;
                }
                return true;
            }

            const SymbolKind kind = parsed.isTypedef
                ? SymbolKind::TypedefName
                : SymbolKind::Variable;
            const std::size_t symbol = createSymbol(
                name, kind, declaredType, declarationToken, scope, true);
            bind(declarationToken, symbol);
            declaredAny = true;

            while (cursor < tokens_.size() && tokens_[cursor].text == "[" &&
                   matching_[cursor] != noIndex)
            {
                cursor = matching_[cursor] + 1;
            }

            int parentheses = 0;
            int braces = 0;
            int brackets = 0;
            while (cursor < tokens_.size())
            {
                const std::string& text = tokens_[cursor].text;
                if (text == "(")
                {
                    ++parentheses;
                }
                else if (text == ")")
                {
                    if (parentheses == 0)
                    {
                        return false;
                    }
                    --parentheses;
                }
                else if (text == "{")
                {
                    ++braces;
                }
                else if (text == "}")
                {
                    if (braces == 0)
                    {
                        return false;
                    }
                    --braces;
                }
                else if (text == "[")
                {
                    ++brackets;
                }
                else if (text == "]")
                {
                    --brackets;
                }
                else if (parentheses == 0 && braces == 0 && brackets == 0 &&
                         (text == "," || text == ";"))
                {
                    break;
                }
                ++cursor;
            }

            if (cursor >= tokens_.size())
            {
                return false;
            }
            if (tokens_[cursor].text == ";")
            {
                consumedEnd = cursor;
                return declaredAny;
            }
            ++cursor;
        }
        return false;
    }

    TypeRef inferTypeBefore(std::size_t operatorIndex) const
    {
        TypeRef unknown;
        if (operatorIndex == 0)
        {
            return unknown;
        }

        std::size_t cursor = operatorIndex - 1;
        if (tokens_[cursor].text == "]" && matching_[cursor] != noIndex)
        {
            const std::size_t open = matching_[cursor];
            if (open > 0)
            {
                cursor = open - 1;
            }
            else
            {
                return unknown;
            }
        }
        else if (tokens_[cursor].text == ")" &&
                 matching_[cursor] != noIndex)
        {
            const std::size_t open = matching_[cursor];
            if (open > 0 && tokens_[open - 1].isIdentifier)
            {
                const std::size_t function = binding(open - 1);
                if (function != noIndex &&
                    symbols_[function].kind == SymbolKind::Function)
                {
                    return symbols_[function].type;
                }
            }

            for (std::size_t nested = cursor; nested > open + 1;)
            {
                --nested;
                if (tokens_[nested].isIdentifier)
                {
                    const std::size_t symbol = binding(nested);
                    if (symbol != noIndex)
                    {
                        return symbols_[symbol].type;
                    }
                }
            }
            return unknown;
        }

        if (!tokens_[cursor].isIdentifier)
        {
            return unknown;
        }
        const std::size_t symbol = binding(cursor);
        return symbol == noIndex ? unknown : symbols_[symbol].type;
    }

    void resolveIdentifier(std::size_t index, std::size_t scope)
    {
        if (!tokens_[index].isIdentifier || binding(index) != noIndex ||
            protectedSource_[tokens_[index].sourceIndex])
        {
            return;
        }

        if (index > 0 &&
            (tokens_[index - 1].text == "." ||
             tokens_[index - 1].text == "->"))
        {
            const TypeRef baseType = inferTypeBefore(index - 1);
            if (baseType.kind == TypeKind::Structure &&
                baseType.structure != noIndex)
            {
                const auto field = structures_[baseType.structure].fields.find(
                    tokens_[index].text);
                if (field != structures_[baseType.structure].fields.end())
                {
                    bind(index, field->second);
                }
                return;
            }

            if (baseType.kind == TypeKind::Vector &&
                isVectorSelector(tokens_[index].text))
            {
                protectedSource_[tokens_[index].sourceIndex] = true;
            }
            return;
        }

        const std::size_t symbol = lookupName(
            scope, tokens_[index].text, index);
        if (symbol != noIndex)
        {
            bind(index, symbol);
        }
    }

    void resolveRange(
        std::size_t begin,
        std::size_t end,
        std::size_t scope)
    {
        if (tokens_.empty())
        {
            return;
        }
        end = std::min(end, tokens_.size() - 1);
        for (std::size_t index = begin; index <= end; ++index)
        {
            if (!ignored_[index])
            {
                resolveIdentifier(index, scope);
            }
        }
    }

    bool canStartDeclaration(std::size_t index, std::size_t scope) const
    {
        const std::string& text = tokens_[index].text;
        if (text == "typedef" || text == "struct" || text == "union" ||
            text == "__kernel" || text == "kernel" ||
            isTypeQualifier(text) || isScalarType(text) || isVectorType(text))
        {
            return true;
        }
        return tokens_[index].isIdentifier &&
            (lookupName(scope, text, index, true) != noIndex ||
             isKnownExternalType(text));
    }

    void scan()
    {
        std::vector<std::size_t> scopeStack(1, 0);
        for (std::size_t index = 0; index < tokens_.size();)
        {
            for (unsigned int count = 0; count < forScopePush_[index]; ++count)
            {
                scopeStack.push_back(createScope(scopeStack.back()));
            }

            if (ignored_[index])
            {
                ++index;
                continue;
            }

            if (tokens_[index].text == "{")
            {
                const std::size_t bodyScope = createScope(scopeStack.back());
                scopeStack.push_back(bodyScope);
                const auto parameters = parametersAtBody_.find(index);
                if (parameters != parametersAtBody_.end())
                {
                    for (std::size_t parameter : parameters->second)
                    {
                        addSymbolToScope(bodyScope, parameter);
                    }
                }
                ++index;
                continue;
            }

            if (tokens_[index].text == "}")
            {
                if (scopeStack.size() > 1)
                {
                    scopeStack.pop_back();
                }
                for (unsigned int count = 0;
                     count < forScopePopAfter_[index] && scopeStack.size() > 1;
                     ++count)
                {
                    scopeStack.pop_back();
                }
                ++index;
                continue;
            }

            const std::size_t currentScope = scopeStack.back();
            if (canStartDeclaration(index, currentScope))
            {
                std::size_t consumedEnd = noIndex;
                if (tryParseDeclaration(index, currentScope, consumedEnd))
                {
                    resolveRange(index, consumedEnd, currentScope);
                    const unsigned int scopesToPop =
                        forScopePopAfter_[consumedEnd];
                    for (unsigned int count = 0;
                         count < scopesToPop && scopeStack.size() > 1;
                         ++count)
                    {
                        scopeStack.pop_back();
                    }
                    index = consumedEnd + 1;
                    continue;
                }
            }

            resolveIdentifier(index, currentScope);

            for (unsigned int count = 0;
                 count < forScopePopAfter_[index] && scopeStack.size() > 1;
                 ++count)
            {
                scopeStack.pop_back();
            }
            ++index;
        }
    }

    void bindForwardFunctionCalls()
    {
        std::map<std::string, std::size_t> functions;
        for (const auto& entry : scopes_[0].names)
        {
            for (std::size_t symbol : entry.second)
            {
                if (symbols_[symbol].kind == SymbolKind::Function)
                {
                    functions[entry.first] = symbol;
                }
            }
        }

        for (std::size_t index = 0; index + 1 < tokens_.size(); ++index)
        {
            if (!tokens_[index].isIdentifier || binding(index) != noIndex ||
                tokens_[index + 1].text != "(" ||
                (index > 0 &&
                 (tokens_[index - 1].text == "." ||
                  tokens_[index - 1].text == "->")))
            {
                continue;
            }
            const auto function = functions.find(tokens_[index].text);
            if (function != functions.end())
            {
                bind(index, function->second);
            }
        }
    }

    std::string makeUniqueName(const std::string& original)
    {
        static const char alphanumeric[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        static const char alphabetic[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

        const std::size_t length = std::max<std::size_t>(
            1, (minGeneratedNameLength + original.size()) %
                   maxGeneratedNameLength);
        std::string result;
        do
        {
            result.clear();
            result.reserve(length);
            for (std::size_t index = 0; index < length; ++index)
            {
                const char* alphabet = index == 0 ? alphabetic : alphanumeric;
                const std::size_t alphabetSize = index == 0
                    ? sizeof(alphabetic) - 1
                    : sizeof(alphanumeric) - 1;
                result.push_back(alphabet[std::rand() % alphabetSize]);
            }
        } while (originalIdentifiers_.find(result) != originalIdentifiers_.end() ||
                 generatedNames_.find(result) != generatedNames_.end());

        generatedNames_.insert(result);
        return result;
    }
};
}

std::vector<std::string> ResolveIdentifierNames(
    const std::vector<IdentifierSourceToken>& sourceTokens)
{
    Resolver resolver(sourceTokens);
    return resolver.run();
}
