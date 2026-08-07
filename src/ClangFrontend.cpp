#include "ClangFrontend.hpp"

#ifdef OPEN_SLEX_HAS_CLANG_FRONTEND

#include <clang-c/Index.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct SourceEdit
{
    unsigned int offset = 0;
    unsigned int length = 0;
    std::string replacement;
};

struct Symbol
{
    std::string identity;
    std::string spelling;
    unsigned int declarationOffset = 0;
    bool renameable = true;
    std::string replacement;
};

struct Location
{
    bool valid = false;
    unsigned int offset = 0;
};

std::string consumeString(CXString value)
{
    const char* text = clang_getCString(value);
    const std::string result = text != nullptr ? text : "";
    clang_disposeString(value);
    return result;
}

bool isIdentifierStart(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalpha(value) != 0 || character == '_';
}

bool isIdentifierCharacter(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

bool hasIdentifierBoundaries(
    const std::string& source,
    std::size_t offset,
    std::size_t length)
{
    const bool validLeft = offset == 0 ||
        !isIdentifierCharacter(source[offset - 1]);
    const std::size_t end = offset + length;
    const bool validRight = end >= source.size() ||
        !isIdentifierCharacter(source[end]);
    return validLeft && validRight;
}

std::set<std::string> collectSourceIdentifiers(const std::string& source)
{
    std::set<std::string> identifiers;
    std::size_t index = 0;
    while (index < source.size())
    {
        if (!isIdentifierStart(source[index]))
        {
            ++index;
            continue;
        }

        const std::size_t start = index++;
        while (index < source.size() &&
               isIdentifierCharacter(source[index]))
        {
            ++index;
        }
        identifiers.insert(source.substr(start, index - start));
    }
    return identifiers;
}

void collectIdentifiers(
    const std::string& text,
    std::size_t start,
    const std::set<std::string>& excluded,
    std::set<std::string>& destination)
{
    std::size_t index = start;
    while (index < text.size())
    {
        if (!isIdentifierStart(text[index]))
        {
            ++index;
            continue;
        }

        const std::size_t identifierStart = index++;
        while (index < text.size() && isIdentifierCharacter(text[index]))
        {
            ++index;
        }
        const std::string identifier = text.substr(
            identifierStart, index - identifierStart);
        if (excluded.find(identifier) == excluded.end())
        {
            destination.insert(identifier);
        }
    }
}

std::set<std::string> collectMacroReplacementReferences(
    const std::string& source)
{
    std::set<std::string> references;
    std::size_t lineStart = 0;
    while (lineStart < source.size())
    {
        std::size_t logicalEnd = source.find('\n', lineStart);
        if (logicalEnd == std::string::npos)
        {
            logicalEnd = source.size();
        }

        std::size_t physicalEnd = logicalEnd;
        while (physicalEnd > lineStart &&
               source[physicalEnd - 1] == '\r')
        {
            --physicalEnd;
        }
        while (physicalEnd > lineStart &&
               source[physicalEnd - 1] == '\\' &&
               logicalEnd < source.size())
        {
            const std::size_t nextEnd = source.find('\n', logicalEnd + 1);
            logicalEnd = nextEnd == std::string::npos
                ? source.size()
                : nextEnd;
            physicalEnd = logicalEnd;
            while (physicalEnd > lineStart &&
                   source[physicalEnd - 1] == '\r')
            {
                --physicalEnd;
            }
        }

        const std::string directive = source.substr(
            lineStart, logicalEnd - lineStart);
        std::size_t cursor = 0;
        while (cursor < directive.size() &&
               (directive[cursor] == ' ' || directive[cursor] == '\t'))
        {
            ++cursor;
        }
        if (cursor >= directive.size() || directive[cursor] != '#')
        {
            lineStart = logicalEnd < source.size() ? logicalEnd + 1 : source.size();
            continue;
        }

        ++cursor;
        while (cursor < directive.size() &&
               std::isspace(static_cast<unsigned char>(directive[cursor])) != 0)
        {
            ++cursor;
        }
        const std::size_t keywordStart = cursor;
        while (cursor < directive.size() &&
               isIdentifierCharacter(directive[cursor]))
        {
            ++cursor;
        }
        if (directive.substr(keywordStart, cursor - keywordStart) != "define")
        {
            lineStart = logicalEnd < source.size() ? logicalEnd + 1 : source.size();
            continue;
        }

        while (cursor < directive.size() &&
               std::isspace(static_cast<unsigned char>(directive[cursor])) != 0)
        {
            ++cursor;
        }
        if (cursor >= directive.size() || !isIdentifierStart(directive[cursor]))
        {
            lineStart = logicalEnd < source.size() ? logicalEnd + 1 : source.size();
            continue;
        }

        const std::size_t macroStart = cursor++;
        while (cursor < directive.size() &&
               isIdentifierCharacter(directive[cursor]))
        {
            ++cursor;
        }

        std::set<std::string> excluded;
        excluded.insert(directive.substr(macroStart, cursor - macroStart));
        if (cursor < directive.size() && directive[cursor] == '(')
        {
            const std::size_t parametersStart = ++cursor;
            unsigned int depth = 1;
            while (cursor < directive.size() && depth != 0)
            {
                if (directive[cursor] == '(')
                {
                    ++depth;
                }
                else if (directive[cursor] == ')')
                {
                    --depth;
                }
                ++cursor;
            }
            const std::size_t parametersEnd = depth == 0 ? cursor - 1 : cursor;
            collectIdentifiers(
                directive.substr(
                    parametersStart, parametersEnd - parametersStart),
                0,
                std::set<std::string>{},
                excluded);
        }

        collectIdentifiers(directive, cursor, excluded, references);
        lineStart = logicalEnd < source.size() ? logicalEnd + 1 : source.size();
    }
    return references;
}

bool rangeOffsets(
    CXSourceRange range,
    std::size_t sourceSize,
    unsigned int& start,
    unsigned int& end);

void collectSkippedRangeIdentifiers(
    CXTranslationUnit translationUnit,
    const std::string& source,
    std::set<std::string>& destination)
{
    CXSourceRangeList* ranges = clang_getAllSkippedRanges(translationUnit);
    if (ranges == nullptr)
    {
        return;
    }
    const std::set<std::string> noExcludedIdentifiers;
    for (unsigned int index = 0; index < ranges->count; ++index)
    {
        unsigned int start = 0;
        unsigned int end = 0;
        if (rangeOffsets(ranges->ranges[index], source.size(), start, end) &&
            start < end && end <= source.size())
        {
            collectIdentifiers(
                source.substr(start, end - start),
                0,
                noExcludedIdentifiers,
                destination);
        }
    }
    clang_disposeSourceRangeList(ranges);
}

Location plainLocation(CXSourceLocation location, std::size_t sourceSize)
{
    CXFile spellingFile = nullptr;
    CXFile expansionFile = nullptr;
    unsigned int spellingOffset = 0;
    unsigned int expansionOffset = 0;
    clang_getSpellingLocation(
        location, &spellingFile, nullptr, nullptr, &spellingOffset);
    clang_getExpansionLocation(
        location, &expansionFile, nullptr, nullptr, &expansionOffset);

    if (spellingFile == nullptr || expansionFile == nullptr ||
        spellingFile != expansionFile || spellingOffset != expansionOffset ||
        clang_Location_isFromMainFile(location) == 0 ||
        spellingOffset > sourceSize)
    {
        return Location{};
    }
    return Location{true, spellingOffset};
}

Location sourceSpellingLocation(
    CXSourceLocation location,
    std::size_t sourceSize)
{
    CXFile spellingFile = nullptr;
    CXFile expansionFile = nullptr;
    unsigned int spellingOffset = 0;
    unsigned int expansionOffset = 0;
    clang_getSpellingLocation(
        location, &spellingFile, nullptr, nullptr, &spellingOffset);
    clang_getExpansionLocation(
        location, &expansionFile, nullptr, nullptr, &expansionOffset);
    const bool isMainFileLocation =
        clang_Location_isFromMainFile(location) != 0;
    const bool isMacroArgumentInMainFile =
        spellingFile != nullptr && spellingFile == expansionFile &&
        spellingOffset != expansionOffset;
    if (spellingFile == nullptr ||
        (!isMainFileLocation && !isMacroArgumentInMainFile) ||
        spellingOffset > sourceSize)
    {
        return Location{};
    }
    return Location{true, spellingOffset};
}

bool rangeOffsets(
    CXSourceRange range,
    std::size_t sourceSize,
    unsigned int& start,
    unsigned int& end)
{
    const Location startLocation = plainLocation(
        clang_getRangeStart(range), sourceSize);
    const Location endLocation = plainLocation(
        clang_getRangeEnd(range), sourceSize);
    if (!startLocation.valid || !endLocation.valid ||
        startLocation.offset > endLocation.offset)
    {
        return false;
    }
    start = startLocation.offset;
    end = endLocation.offset;
    return true;
}

bool locateCursorSpelling(
    CXCursor cursor,
    const std::string& spelling,
    const std::string& source,
    unsigned int& offset)
{
    if (spelling.empty())
    {
        return false;
    }

    const CXSourceLocation clangLocation = clang_getCursorLocation(cursor);
    const Location spellingLocation = sourceSpellingLocation(
        clangLocation, source.size());
    if (spellingLocation.valid &&
        spellingLocation.offset + spelling.size() <= source.size() &&
        source.compare(
            spellingLocation.offset, spelling.size(), spelling) == 0 &&
        hasIdentifierBoundaries(
            source, spellingLocation.offset, spelling.size()))
    {
        offset = spellingLocation.offset;
        return true;
    }

    const Location cursorLocation = plainLocation(
        clangLocation, source.size());
    if (!cursorLocation.valid)
    {
        return false;
    }

    if (cursorLocation.offset + spelling.size() <= source.size() &&
        source.compare(cursorLocation.offset, spelling.size(), spelling) == 0 &&
        hasIdentifierBoundaries(
            source, cursorLocation.offset, spelling.size()))
    {
        offset = cursorLocation.offset;
        return true;
    }

    unsigned int rangeStart = 0;
    unsigned int rangeEnd = 0;
    if (!rangeOffsets(
            clang_getCursorExtent(cursor), source.size(), rangeStart, rangeEnd))
    {
        return false;
    }

    const std::size_t searchStart = std::max<std::size_t>(
        rangeStart,
        cursorLocation.offset > 64 ? cursorLocation.offset - 64 : 0);
    const std::size_t searchEnd = std::min<std::size_t>(
        rangeEnd,
        cursorLocation.offset + spelling.size() + 128);
    std::size_t candidate = source.find(spelling, searchStart);
    std::size_t best = std::string::npos;
    std::size_t bestDistance = source.size();
    while (candidate != std::string::npos &&
           candidate + spelling.size() <= searchEnd)
    {
        if (hasIdentifierBoundaries(source, candidate, spelling.size()))
        {
            const std::size_t distance = candidate > cursorLocation.offset
                ? candidate - cursorLocation.offset
                : cursorLocation.offset - candidate;
            if (distance < bestDistance)
            {
                best = candidate;
                bestDistance = distance;
            }
        }
        candidate = source.find(spelling, candidate + 1);
    }
    if (best == std::string::npos)
    {
        return false;
    }
    offset = static_cast<unsigned int>(best);
    return true;
}

std::string cursorIdentity(CXCursor cursor, const std::string& source)
{
    const std::string usr = consumeString(clang_getCursorUSR(cursor));
    if (!usr.empty())
    {
        return usr;
    }

    const Location location = plainLocation(
        clang_getCursorLocation(cursor), source.size());
    if (!location.valid)
    {
        return "";
    }

    std::ostringstream stream;
    stream << "local:" << static_cast<unsigned int>(clang_getCursorKind(cursor))
           << ':' << location.offset << ':'
           << consumeString(clang_getCursorSpelling(cursor));
    return stream.str();
}

bool isRenameableDeclarationKind(CXCursorKind kind)
{
    switch (kind)
    {
        case CXCursor_StructDecl:
        case CXCursor_UnionDecl:
        case CXCursor_EnumDecl:
        case CXCursor_FieldDecl:
        case CXCursor_EnumConstantDecl:
        case CXCursor_FunctionDecl:
        case CXCursor_VarDecl:
        case CXCursor_ParmDecl:
        case CXCursor_TypedefDecl:
            return true;
        default:
            return false;
    }
}

bool containsIdentifier(
    const std::string& text,
    const std::string& identifier)
{
    std::size_t offset = text.find(identifier);
    while (offset != std::string::npos)
    {
        const bool validLeft = offset == 0 ||
            !isIdentifierCharacter(text[offset - 1]);
        const std::size_t end = offset + identifier.size();
        const bool validRight = end >= text.size() ||
            !isIdentifierCharacter(text[end]);
        if (validLeft && validRight)
        {
            return true;
        }
        offset = text.find(identifier, offset + 1);
    }
    return false;
}

bool isKernelFunction(CXCursor cursor, const std::string& source)
{
    if (clang_getCursorKind(cursor) != CXCursor_FunctionDecl)
    {
        return false;
    }

    const std::string functionType = consumeString(clang_getTypeSpelling(
        clang_getCursorType(cursor)));
    if (containsIdentifier(functionType, "device_kernel"))
    {
        return true;
    }

    unsigned int start = 0;
    unsigned int end = 0;
    unsigned int nameOffset = 0;
    const std::string spelling = consumeString(clang_getCursorSpelling(cursor));
    if (!rangeOffsets(
            clang_getCursorExtent(cursor), source.size(), start, end) ||
        !locateCursorSpelling(cursor, spelling, source, nameOffset) ||
        nameOffset < start)
    {
        return false;
    }

    const std::string prefix = source.substr(start, nameOffset - start);
    return containsIdentifier(prefix, "kernel") ||
        containsIdentifier(prefix, "__kernel");
}

struct CollectionContext
{
    const std::string& source;
    const std::set<std::string>& macroReferences;
    std::vector<Symbol> symbols;
    std::map<std::string, std::size_t> symbolByIdentity;
};

CXChildVisitResult collectSymbolsVisitor(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    CollectionContext& context =
        *static_cast<CollectionContext*>(clientData);
    const Location location = plainLocation(
        clang_getCursorLocation(cursor), context.source.size());
    if (!location.valid)
    {
        return CXChildVisit_Continue;
    }

    const CXCursorKind kind = clang_getCursorKind(cursor);
    if (isRenameableDeclarationKind(kind))
    {
        const std::string spelling = consumeString(
            clang_getCursorSpelling(cursor));
        const std::string identity = cursorIdentity(cursor, context.source);
        unsigned int spellingOffset = 0;
        if (!spelling.empty() && !identity.empty() &&
            locateCursorSpelling(
                cursor, spelling, context.source, spellingOffset))
        {
            const bool renameable =
                context.macroReferences.find(spelling) ==
                    context.macroReferences.end() &&
                !isKernelFunction(cursor, context.source);
            const auto existing = context.symbolByIdentity.find(identity);
            if (existing == context.symbolByIdentity.end())
            {
                const std::size_t index = context.symbols.size();
                context.symbolByIdentity[identity] = index;
                context.symbols.push_back(
                    {identity, spelling, spellingOffset, renameable, ""});
            }
            else
            {
                Symbol& symbol = context.symbols[existing->second];
                symbol.declarationOffset = std::min(
                    symbol.declarationOffset, spellingOffset);
                symbol.renameable = symbol.renameable && renameable;
            }
        }
    }
    return CXChildVisit_Recurse;
}

struct ReferenceProtectionContext
{
    const std::string& source;
    CollectionContext& collection;
};

bool isDirectReferenceKind(CXCursorKind kind)
{
    return kind == CXCursor_DeclRefExpr ||
        kind == CXCursor_MemberRefExpr ||
        kind == CXCursor_TypeRef;
}

CXChildVisitResult protectUneditableReferencesVisitor(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    ReferenceProtectionContext& context =
        *static_cast<ReferenceProtectionContext*>(clientData);
    if (!isDirectReferenceKind(clang_getCursorKind(cursor)))
    {
        return CXChildVisit_Recurse;
    }

    const CXCursor referenced = clang_getCursorReferenced(cursor);
    if (!clang_Cursor_isNull(referenced))
    {
        const std::string identity = cursorIdentity(
            referenced, context.source);
        const auto symbol = context.collection.symbolByIdentity.find(identity);
        if (symbol != context.collection.symbolByIdentity.end())
        {
            Symbol& resolved = context.collection.symbols[symbol->second];
            unsigned int editableOffset = 0;
            const bool editable = locateCursorSpelling(
                    cursor,
                    resolved.spelling,
                    context.source,
                    editableOffset);
            if (!editable)
            {
                resolved.renameable = false;
            }
        }
    }
    return CXChildVisit_Recurse;
}

std::uint32_t nextRandom(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::string makeUniqueName(
    std::uint32_t& randomState,
    std::set<std::string>& reserved)
{
    static const char firstCharacters[] = "lIO";
    static const char remainingCharacters[] = "lIO01";
    for (;;)
    {
        const std::size_t length = 10 + (nextRandom(randomState) % 7);
        std::string candidate;
        candidate.reserve(length);
        candidate.push_back(firstCharacters[
            nextRandom(randomState) % (sizeof(firstCharacters) - 1)]);
        while (candidate.size() < length)
        {
            candidate.push_back(remainingCharacters[
                nextRandom(randomState) %
                    (sizeof(remainingCharacters) - 1)]);
        }
        if (reserved.insert(candidate).second)
        {
            return candidate;
        }
    }
}

void assignGeneratedNames(
    CollectionContext& context,
    std::uint32_t seed,
    std::set<std::string>& reserved)
{
    std::vector<std::size_t> order;
    for (std::size_t index = 0; index < context.symbols.size(); ++index)
    {
        if (context.symbols[index].renameable)
        {
            order.push_back(index);
        }
    }
    std::sort(
        order.begin(), order.end(),
        [&context](std::size_t left, std::size_t right) {
            return context.symbols[left].declarationOffset <
                context.symbols[right].declarationOffset;
        });

    std::uint32_t randomState = seed == 0 ? 0x6d2b79f5u : seed;
    for (std::size_t index : order)
    {
        context.symbols[index].replacement = makeUniqueName(
            randomState, reserved);
    }
}

struct EditContext
{
    const std::string& source;
    const CollectionContext& collection;
    std::vector<SourceEdit>& edits;
    std::set<std::pair<unsigned int, unsigned int>> occupiedRanges;
};

void addSymbolEdit(EditContext& context, CXCursor cursor, const Symbol& symbol)
{
    if (symbol.replacement.empty())
    {
        return;
    }
    unsigned int offset = 0;
    if (!locateCursorSpelling(
            cursor, symbol.spelling, context.source, offset))
    {
        return;
    }

    const auto range = std::make_pair(
        offset, static_cast<unsigned int>(symbol.spelling.size()));
    if (context.occupiedRanges.insert(range).second)
    {
        context.edits.push_back(
            {offset, range.second, symbol.replacement});
    }
}

CXChildVisitResult collectEditsVisitor(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    EditContext& context = *static_cast<EditContext*>(clientData);
    const CXSourceLocation cursorLocation = clang_getCursorLocation(cursor);
    const Location plain = plainLocation(
        cursorLocation, context.source.size());
    const Location spelling = sourceSpellingLocation(
        cursorLocation, context.source.size());
    if (!plain.valid && !spelling.valid)
    {
        return CXChildVisit_Continue;
    }

    std::string identity;
    if (isRenameableDeclarationKind(clang_getCursorKind(cursor)))
    {
        identity = cursorIdentity(cursor, context.source);
    }
    else
    {
        const CXCursor referenced = clang_getCursorReferenced(cursor);
        if (!clang_Cursor_isNull(referenced))
        {
            identity = cursorIdentity(referenced, context.source);
        }
    }

    const auto symbol = context.collection.symbolByIdentity.find(identity);
    if (symbol != context.collection.symbolByIdentity.end())
    {
        addSymbolEdit(
            context, cursor, context.collection.symbols[symbol->second]);
    }
    return CXChildVisit_Recurse;
}

void collectPhysicalTokenEdits(
    CXTranslationUnit translationUnit,
    const std::string& inputPath,
    EditContext& context)
{
    CXFile mainFile = clang_getFile(translationUnit, inputPath.c_str());
    if (mainFile == nullptr)
    {
        return;
    }

    std::size_t offset = 0;
    while (offset < context.source.size())
    {
        if (!isIdentifierStart(context.source[offset]))
        {
            ++offset;
            continue;
        }

        const std::size_t start = offset++;
        while (offset < context.source.size() &&
               isIdentifierCharacter(context.source[offset]))
        {
            ++offset;
        }
        if (start > static_cast<std::size_t>(
                std::numeric_limits<unsigned int>::max()))
        {
            return;
        }

        const CXSourceLocation location = clang_getLocationForOffset(
            translationUnit,
            mainFile,
            static_cast<unsigned int>(start));
        CXCursor cursor = clang_getCursor(translationUnit, location);

        std::string identity;
        if (isRenameableDeclarationKind(clang_getCursorKind(cursor)))
        {
            identity = cursorIdentity(cursor, context.source);
        }
        else
        {
            const CXCursor referenced = clang_getCursorReferenced(cursor);
            if (!clang_Cursor_isNull(referenced))
            {
                identity = cursorIdentity(referenced, context.source);
            }
        }

        const auto symbol = context.collection.symbolByIdentity.find(identity);
        if (symbol == context.collection.symbolByIdentity.end())
        {
            continue;
        }

        const Symbol& resolved = context.collection.symbols[symbol->second];
        const std::size_t length = offset - start;
        if (resolved.replacement.empty() ||
            resolved.spelling.size() != length ||
            context.source.compare(start, length, resolved.spelling) != 0)
        {
            continue;
        }

        const auto range = std::make_pair(
            static_cast<unsigned int>(start),
            static_cast<unsigned int>(length));
        if (context.occupiedRanges.insert(range).second)
        {
            context.edits.push_back({
                range.first, range.second, resolved.replacement});
        }
    }
}

struct CompoundSearch
{
    bool found = false;
    CXCursor cursor = clang_getNullCursor();
};

CXChildVisitResult findFirstCompoundVisitor(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    CompoundSearch& search = *static_cast<CompoundSearch*>(clientData);
    if (clang_getCursorKind(cursor) == CXCursor_CompoundStmt)
    {
        search.found = true;
        search.cursor = cursor;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

std::string unsignedLiteral(std::uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << value << 'u';
    return stream.str();
}

std::string opaquePredicate(std::uint32_t seed, std::uint32_t index)
{
    const std::uint32_t predicateSeed = seed ^ (index * 0x85ebca6bu);
    const std::uint32_t payloadLeft = predicateSeed ^ 0xa5a5a5a5u;
    const std::uint32_t payloadRight = predicateSeed ^ 0x5a5a5a5au;
    const std::string seedLiteral = unsignedLiteral(predicateSeed);

    return "if(((" + seedLiteral + "*(" + seedLiteral +
        "+1u))&1u)!=0u){(void)(" + unsignedLiteral(payloadLeft) +
        "^" + unsignedLiteral(payloadRight) + ");}";
}

struct OpaqueContext
{
    const std::string& source;
    std::uint32_t seed = 0;
    std::uint32_t index = 0;
    std::vector<SourceEdit>& edits;
};

CXChildVisitResult collectOpaqueEditsVisitor(
    CXCursor cursor,
    CXCursor,
    CXClientData clientData)
{
    OpaqueContext& context = *static_cast<OpaqueContext*>(clientData);
    const Location location = plainLocation(
        clang_getCursorLocation(cursor), context.source.size());
    if (!location.valid)
    {
        return CXChildVisit_Continue;
    }

    if (clang_getCursorKind(cursor) == CXCursor_IfStmt)
    {
        CompoundSearch compound;
        clang_visitChildren(cursor, findFirstCompoundVisitor, &compound);
        if (compound.found)
        {
            unsigned int start = 0;
            unsigned int end = 0;
            if (rangeOffsets(
                    clang_getCursorExtent(compound.cursor),
                    context.source.size(), start, end))
            {
                unsigned int insertionOffset = start;
                if (start < context.source.size() &&
                    context.source[start] == '{')
                {
                    insertionOffset = start + 1;
                }
                else if (context.source.compare(start, 2, "<%") == 0)
                {
                    insertionOffset = start + 2;
                }
                else if (context.source.compare(start, 3, "?" "?<") == 0)
                {
                    insertionOffset = start + 3;
                }
                else
                {
                    return CXChildVisit_Recurse;
                }
                context.edits.push_back({
                    insertionOffset,
                    0,
                    opaquePredicate(context.seed, context.index++)});
            }
        }
    }
    return CXChildVisit_Recurse;
}

bool editOrder(const SourceEdit& left, const SourceEdit& right)
{
    if (left.offset != right.offset)
    {
        return left.offset > right.offset;
    }
    return left.length > right.length;
}

bool applyEdits(
    const std::string& source,
    std::vector<SourceEdit>& edits,
    std::string& transformed,
    std::string& error)
{
    std::sort(edits.begin(), edits.end(), editOrder);
    transformed = source;
    unsigned int previousStart = static_cast<unsigned int>(source.size());
    for (const SourceEdit& edit : edits)
    {
        if (edit.offset > source.size() ||
            edit.length > source.size() - edit.offset ||
            edit.offset + edit.length > previousStart)
        {
            error = "Clang produced overlapping or invalid source edits";
            return false;
        }
        transformed.replace(edit.offset, edit.length, edit.replacement);
        previousStart = edit.offset;
    }
    return true;
}

void appendDiagnosticSource(
    std::ostringstream& output,
    CXDiagnostic diagnostic,
    const std::string& source)
{
    const CXSourceLocation location = clang_getDiagnosticLocation(diagnostic);
    CXFile file = nullptr;
    unsigned int line = 0;
    unsigned int column = 0;
    unsigned int offset = 0;
    clang_getExpansionLocation(
        location, &file, &line, &column, &offset);
    if (file == nullptr || clang_Location_isFromMainFile(location) == 0 ||
        offset > source.size())
    {
        return;
    }

    std::size_t lineStart = offset;
    while (lineStart > 0 && source[lineStart - 1] != '\n')
    {
        --lineStart;
    }
    std::size_t lineEnd = source.find('\n', offset);
    if (lineEnd == std::string::npos)
    {
        lineEnd = source.size();
    }
    if (lineEnd > lineStart && source[lineEnd - 1] == '\r')
    {
        --lineEnd;
    }

    output << "  " << line << " | "
           << source.substr(lineStart, lineEnd - lineStart) << '\n'
           << "    | " << std::string(column > 0 ? column - 1 : 0, ' ')
           << "^\n";
}

std::string formatDiagnostics(
    CXTranslationUnit translationUnit,
    const std::string& source,
    bool& hasErrors)
{
    std::ostringstream output;
    const unsigned int count = clang_getNumDiagnostics(translationUnit);
    for (unsigned int index = 0; index < count; ++index)
    {
        CXDiagnostic diagnostic = clang_getDiagnostic(translationUnit, index);
        const CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(
            diagnostic);
        if (severity == CXDiagnostic_Error || severity == CXDiagnostic_Fatal)
        {
            hasErrors = true;
        }
        output << consumeString(clang_formatDiagnostic(
            diagnostic, clang_defaultDiagnosticDisplayOptions())) << '\n';
        appendDiagnosticSource(output, diagnostic, source);
        clang_disposeDiagnostic(diagnostic);
    }
    return output.str();
}
} // namespace

bool HasClangSemanticFrontend()
{
    return true;
}

ClangFrontendResult RunClangSemanticFrontend(
    const std::string& inputPath,
    const std::string& source,
    const ClangFrontendOptions& options)
{
    ClangFrontendResult result;
    if (source.size() > static_cast<std::size_t>(
            std::numeric_limits<unsigned int>::max()) ||
        source.size() > static_cast<std::size_t>(
            std::numeric_limits<unsigned long>::max()))
    {
        result.diagnostics =
            "The input is too large for libclang source offsets.";
        return result;
    }

    CXIndex index = clang_createIndex(0, 0);
    if (index == nullptr)
    {
        result.diagnostics = "Could not initialize libclang.";
        return result;
    }

    std::vector<std::string> arguments{
        "-x",
        "cl",
        "-cl-std=CL1.2",
        "-trigraphs",
        "-resource-dir=" OPEN_SLEX_CLANG_RESOURCE_DIR,
        "-Xclang",
        "-finclude-default-header",
        "-Werror=unknown-escape-sequence",
    };
    arguments.insert(
        arguments.end(),
        options.compilerArguments.begin(),
        options.compilerArguments.end());

    std::vector<const char*> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (const std::string& argument : arguments)
    {
        argumentPointers.push_back(argument.c_str());
    }

    CXUnsavedFile unsavedFile;
    unsavedFile.Filename = inputPath.c_str();
    unsavedFile.Contents = source.c_str();
    unsavedFile.Length = static_cast<unsigned long>(source.size());

    CXTranslationUnit translationUnit = nullptr;
    const CXErrorCode parseResult = clang_parseTranslationUnit2(
        index,
        inputPath.c_str(),
        argumentPointers.data(),
        static_cast<int>(argumentPointers.size()),
        &unsavedFile,
        1,
        CXTranslationUnit_KeepGoing,
        &translationUnit);
    if (parseResult != CXError_Success || translationUnit == nullptr)
    {
        std::ostringstream message;
        message << "libclang could not parse the translation unit (error "
                << static_cast<int>(parseResult) << ").";
        result.diagnostics = message.str();
        if (translationUnit != nullptr)
        {
            clang_disposeTranslationUnit(translationUnit);
        }
        clang_disposeIndex(index);
        return result;
    }

    bool hasErrors = false;
    result.diagnostics = formatDiagnostics(
        translationUnit, source, hasErrors);
    if (!hasErrors)
    {
        std::set<std::string> protectedIdentifiers =
            collectMacroReplacementReferences(source);
        collectSkippedRangeIdentifiers(
            translationUnit, source, protectedIdentifiers);
        CollectionContext collection{
            source,
            protectedIdentifiers,
            {},
            {}};
        clang_visitChildren(
            clang_getTranslationUnitCursor(translationUnit),
            collectSymbolsVisitor,
            &collection);

        ReferenceProtectionContext referenceProtection{
            source,
            collection};
        clang_visitChildren(
            clang_getTranslationUnitCursor(translationUnit),
            protectUneditableReferencesVisitor,
            &referenceProtection);

        std::set<std::string> reserved = collectSourceIdentifiers(source);
        assignGeneratedNames(collection, options.seed, reserved);

        std::vector<SourceEdit> edits;
        EditContext editContext{
            source,
            collection,
            edits,
            {}};
        clang_visitChildren(
            clang_getTranslationUnitCursor(translationUnit),
            collectEditsVisitor,
            &editContext);
        collectPhysicalTokenEdits(
            translationUnit, inputPath, editContext);

        if (options.insertOpaquePredicates)
        {
            OpaqueContext opaqueContext{
                source,
                options.seed == 0 ? 0x6d2b79f5u : options.seed,
                0,
                edits};
            clang_visitChildren(
                clang_getTranslationUnitCursor(translationUnit),
                collectOpaqueEditsVisitor,
                &opaqueContext);
        }

        std::string editError;
        if (applyEdits(
                source, edits, result.transformedSource, editError))
        {
            result.status = ClangFrontendStatus::Success;
        }
        else
        {
            result.diagnostics += editError + ".\n";
        }
    }

    if (hasErrors)
    {
        result.status = ClangFrontendStatus::SyntaxError;
    }

    clang_disposeTranslationUnit(translationUnit);
    clang_disposeIndex(index);
    return result;
}

#else

bool HasClangSemanticFrontend()
{
    return false;
}

ClangFrontendResult RunClangSemanticFrontend(
    const std::string&,
    const std::string&,
    const ClangFrontendOptions&)
{
    ClangFrontendResult result;
    result.diagnostics =
        "This OpenSLex build does not contain the Clang semantic frontend.";
    return result;
}

#endif
