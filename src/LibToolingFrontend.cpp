#include "LibToolingFrontend.hpp"

#ifdef OPEN_SLEX_HAS_LIBTOOLING_FRONTEND

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Core/Replacement.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using clang::ASTContext;
using clang::CharSourceRange;
using clang::Decl;
using clang::DeclRefExpr;
using clang::DesignatedInitExpr;
using clang::Diagnostic;
using clang::DiagnosticConsumer;
using clang::EnumConstantDecl;
using clang::EnumDecl;
using clang::FieldDecl;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::LangOptions;
using clang::MemberExpr;
using clang::NamedDecl;
using clang::RecordDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::TagTypeLoc;
using clang::TypedefNameDecl;
using clang::TypedefTypeLoc;
using clang::VarDecl;

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

void collectIdentifiers(llvm::StringRef text, std::set<std::string>& destination)
{
    std::size_t index = 0;
    while (index < text.size())
    {
        if (!isIdentifierStart(text[index]))
        {
            ++index;
            continue;
        }

        const std::size_t start = index++;
        while (index < text.size() && isIdentifierCharacter(text[index]))
        {
            ++index;
        }
        destination.insert(text.substr(start, index - start).str());
    }
}

std::set<std::string> collectSourceIdentifiers(const std::string& source)
{
    std::set<std::string> identifiers;
    collectIdentifiers(source, identifiers);
    return identifiers;
}

struct SkippedIdentifier
{
    std::string  spelling;
    unsigned int length = 0;
};

bool isPreprocessorDirectiveLine(const std::string& source, std::size_t offset)
{
    if (offset >= source.size())
    {
        return false;
    }

    const std::size_t precedingNewline = offset == 0 ? std::string::npos : source.rfind('\n', offset - 1);
    std::size_t       lineStart        = precedingNewline == std::string::npos ? 0 : precedingNewline + 1;

    for (;;)
    {
        std::size_t first = lineStart;
        while (first < source.size() && (source[first] == ' ' || source[first] == '\t' || source[first] == '\v' || source[first] == '\f' || source[first] == '\r'))
        {
            ++first;
        }
        const bool startsDirective = first < source.size() && (source[first] == '#' || (first + 1 < source.size() && source[first] == '%' && source[first + 1] == ':') ||
                                                                  (first + 2 < source.size() && source[first] == '?' && source[first + 1] == '?' && source[first + 2] == '='));
        if (startsDirective)
        {
            return true;
        }

        if (lineStart == 0)
        {
            return false;
        }

        const std::size_t previousNewline = lineStart - 1;
        std::size_t       previousEnd     = previousNewline;
        while (previousEnd > 0 && (source[previousEnd - 1] == ' ' || source[previousEnd - 1] == '\t' || source[previousEnd - 1] == '\v' || source[previousEnd - 1] == '\f' ||
                                      source[previousEnd - 1] == '\r'))
        {
            --previousEnd;
        }
        const bool continued = previousEnd > 0 && (source[previousEnd - 1] == '\\' ||
                                                      (previousEnd >= 3 && source[previousEnd - 3] == '?' && source[previousEnd - 2] == '?' && source[previousEnd - 1] == '/'));
        if (!continued)
        {
            return false;
        }

        const std::size_t earlierNewline = previousNewline == 0 ? std::string::npos : source.rfind('\n', previousNewline - 1);
        lineStart                        = earlierNewline == std::string::npos ? 0 : earlierNewline + 1;
    }
}

void collectSkippedIdentifiers(llvm::StringRef text, unsigned int baseOffset, const std::string& source, std::map<unsigned int, SkippedIdentifier>& destination)
{
    enum class ScanState {
        Normal,
        LineComment,
        BlockComment,
        StringLiteral,
        CharacterLiteral,
    };

    ScanState   state = ScanState::Normal;
    std::size_t index = 0;
    while (index < text.size())
    {
        const char current = text[index];
        const char next    = index + 1 < text.size() ? text[index + 1] : '\0';

        if (state == ScanState::LineComment)
        {
            if (current == '\n')
            {
                state = ScanState::Normal;
            }
            ++index;
            continue;
        }
        if (state == ScanState::BlockComment)
        {
            if (current == '*' && next == '/')
            {
                state = ScanState::Normal;
                index += 2;
            }
            else
            {
                ++index;
            }
            continue;
        }
        if (state == ScanState::StringLiteral || state == ScanState::CharacterLiteral)
        {
            const char terminator = state == ScanState::StringLiteral ? '"' : '\'';
            if (current == '\\' && index + 1 < text.size())
            {
                index += 2;
            }
            else
            {
                if (current == terminator || current == '\n')
                {
                    state = ScanState::Normal;
                }
                ++index;
            }
            continue;
        }

        if (current == '/' && next == '/')
        {
            state = ScanState::LineComment;
            index += 2;
            continue;
        }
        if (current == '/' && next == '*')
        {
            state = ScanState::BlockComment;
            index += 2;
            continue;
        }
        if (current == '"')
        {
            state = ScanState::StringLiteral;
            ++index;
            continue;
        }
        if (current == '\'')
        {
            state = ScanState::CharacterLiteral;
            ++index;
            continue;
        }
        if (!isIdentifierStart(current))
        {
            ++index;
            continue;
        }

        const std::size_t start = index++;
        while (index < text.size() && isIdentifierCharacter(text[index]))
        {
            ++index;
        }

        const std::size_t absoluteOffset = static_cast<std::size_t>(baseOffset) + start;
        if (absoluteOffset > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()) || isPreprocessorDirectiveLine(source, absoluteOffset))
        {
            continue;
        }

        const unsigned int tokenOffset = static_cast<unsigned int>(absoluteOffset);
        const unsigned int tokenLength = static_cast<unsigned int>(index - start);
        destination.emplace(tokenOffset, SkippedIdentifier{ text.substr(start, index - start).str(), tokenLength });
    }
}

std::uint32_t nextRandom(std::uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::string makeUniqueName(std::uint32_t& randomState, std::set<std::string>& reserved)
{
    static const char firstCharacters[]     = "lIO";
    static const char remainingCharacters[] = "lIO01";
    for (;;)
    {
        const std::size_t length = 10 + (nextRandom(randomState) % 7);
        std::string       candidate;
        candidate.reserve(length);
        candidate.push_back(firstCharacters[nextRandom(randomState) % (sizeof(firstCharacters) - 1)]);
        while (candidate.size() < length)
        {
            candidate.push_back(remainingCharacters[nextRandom(randomState) % (sizeof(remainingCharacters) - 1)]);
        }
        if (reserved.insert(candidate).second)
        {
            return candidate;
        }
    }
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
    const std::uint32_t payloadLeft   = predicateSeed ^ 0xa5a5a5a5u;
    const std::uint32_t payloadRight  = predicateSeed ^ 0x5a5a5a5au;
    const std::string   seedLiteral   = unsignedLiteral(predicateSeed);

    return "if(((" + seedLiteral + "*(" + seedLiteral + "+1u))&1u)!=0u){(void)(" + unsignedLiteral(payloadLeft) + "^" + unsignedLiteral(payloadRight) + ");}";
}

struct FrontendState
{
    FrontendState(const std::string& inputPathValue, const std::string& sourceValue, const LibToolingFrontendOptions& optionsValue)
        : inputPath(inputPathValue), source(sourceValue), options(optionsValue)
    {}

    const std::string&                        inputPath;
    const std::string&                        source;
    const LibToolingFrontendOptions&          options;
    std::set<std::string>                     preprocessorProtectedNames;
    std::map<unsigned int, SkippedIdentifier> skippedIdentifiers;
    clang::tooling::Replacements              replacements;
    std::string                               transformedSource;
    std::string                               internalError;
    bool                                      completed = false;
};

SourceLocation editableSpellingLocation(SourceLocation location, const SourceManager& sourceManager)
{
    if (location.isInvalid())
    {
        return {};
    }
    if (location.isMacroID() && !sourceManager.isMacroArgExpansion(location))
    {
        return {};
    }

    const SourceLocation spelling = sourceManager.getSpellingLoc(location);
    if (spelling.isInvalid() || !sourceManager.isWrittenInMainFile(spelling))
    {
        return {};
    }
    return spelling;
}

const NamedDecl* canonicalNamedDecl(const NamedDecl* declaration)
{
    if (declaration == nullptr)
    {
        return nullptr;
    }
    return llvm::dyn_cast<NamedDecl>(declaration->getCanonicalDecl());
}

bool isRenameableDeclaration(const NamedDecl* declaration)
{
    return llvm::isa<FunctionDecl>(declaration) || llvm::isa<VarDecl>(declaration) || llvm::isa<FieldDecl>(declaration) || llvm::isa<TypedefNameDecl>(declaration) ||
           llvm::isa<RecordDecl>(declaration) || llvm::isa<EnumDecl>(declaration) || llvm::isa<EnumConstantDecl>(declaration);
}

bool isKernelFunction(const NamedDecl* declaration)
{
    const auto* function = llvm::dyn_cast<FunctionDecl>(declaration);
    if (function == nullptr)
    {
        return false;
    }
#if CLANG_VERSION_MAJOR >= 21
    const auto* attribute = function->getAttr<clang::DeviceKernelAttr>();
    return attribute != nullptr && clang::DeviceKernelAttr::isOpenCLSpelling(attribute);
#else
    return function->hasAttr<clang::OpenCLKernelAttr>();
#endif
}

const TypedefNameDecl* typedefDeclaration(TypedefTypeLoc location)
{
#if CLANG_VERSION_MAJOR >= 22
    return location.getDecl();
#else
    return location.getTypedefNameDecl();
#endif
}

struct Symbol
{
    const NamedDecl* declaration = nullptr;
    std::string      spelling;
    unsigned int     declarationOffset = 0;
    bool             renameable        = true;
    std::string      replacement;
};

using SymbolMap = std::map<const NamedDecl*, Symbol>;

class PreprocessorTracker final : public clang::PPCallbacks
{
public:
    PreprocessorTracker(const SourceManager& sourceManager, const LangOptions& languageOptions, FrontendState& state)
        : sourceManager_(sourceManager), languageOptions_(languageOptions), state_(state)
    {}

    void MacroDefined(const clang::Token& macroName, const clang::MacroDirective* directive) override
    {
        if (directive == nullptr || directive->getMacroInfo() == nullptr)
        {
            return;
        }

        const SourceLocation definitionLocation = sourceManager_.getSpellingLoc(macroName.getLocation());
        if (definitionLocation.isInvalid() || sourceManager_.isInSystemHeader(definitionLocation))
        {
            return;
        }

        const clang::MacroInfo&                macro = *directive->getMacroInfo();
        std::set<const clang::IdentifierInfo*> parameters;
        for (const clang::IdentifierInfo* parameter : macro.params())
        {
            parameters.insert(parameter);
        }

        for (const clang::Token& token : macro.tokens())
        {
            const clang::IdentifierInfo* identifier = token.getIdentifierInfo();
            if (identifier != nullptr && parameters.find(identifier) == parameters.end())
            {
                state_.preprocessorProtectedNames.insert(identifier->getName().str());
            }
        }
    }

    void SourceRangeSkipped(SourceRange range, SourceLocation) override
    {
        const SourceLocation begin = sourceManager_.getSpellingLoc(range.getBegin());
        const SourceLocation end   = sourceManager_.getSpellingLoc(range.getEnd());
        if (begin.isInvalid() || end.isInvalid() || !sourceManager_.isWrittenInMainFile(begin) || !sourceManager_.isWrittenInMainFile(end))
        {
            return;
        }

        bool                  invalid = false;
        const llvm::StringRef text    = clang::Lexer::getSourceText(CharSourceRange::getTokenRange(begin, end), sourceManager_, languageOptions_, &invalid);
        if (!invalid)
        {
            collectSkippedIdentifiers(text, sourceManager_.getFileOffset(begin), state_.source, state_.skippedIdentifiers);
        }
    }

private:
    const SourceManager& sourceManager_;
    const LangOptions&   languageOptions_;
    FrontendState&       state_;
};

class SymbolCollector final : public clang::RecursiveASTVisitor<SymbolCollector>
{
public:
    SymbolCollector(ASTContext& context, FrontendState& state) : sourceManager_(context.getSourceManager()), state_(state) {}

    bool VisitNamedDecl(NamedDecl* declaration)
    {
        if (declaration == nullptr || declaration->isImplicit() || declaration->getIdentifier() == nullptr || !isRenameableDeclaration(declaration))
        {
            return true;
        }

        const SourceLocation location = editableSpellingLocation(declaration->getLocation(), sourceManager_);
        if (location.isInvalid())
        {
            return true;
        }

        const NamedDecl* canonical = canonicalNamedDecl(declaration);
        if (canonical == nullptr)
        {
            return true;
        }

        const std::string  spelling   = declaration->getNameAsString();
        const bool         renameable = !isKernelFunction(declaration) && state_.preprocessorProtectedNames.find(spelling) == state_.preprocessorProtectedNames.end();
        const unsigned int offset     = sourceManager_.getFileOffset(location);

        const auto existing = symbols_.find(canonical);
        if (existing == symbols_.end())
        {
            symbols_.emplace(canonical, Symbol{ canonical, spelling, offset, renameable, {} });
        }
        else
        {
            existing->second.declarationOffset = std::min(existing->second.declarationOffset, offset);
            existing->second.renameable        = existing->second.renameable && renameable;
        }
        return true;
    }

    SymbolMap takeSymbols() { return std::move(symbols_); }

private:
    const SourceManager& sourceManager_;
    FrontendState&       state_;
    SymbolMap            symbols_;
};

class ReferenceProtector final : public clang::RecursiveASTVisitor<ReferenceProtector>
{
public:
    ReferenceProtector(const SourceManager& sourceManager, SymbolMap& symbols) : sourceManager_(sourceManager), symbols_(symbols) {}

    bool VisitNamedDecl(NamedDecl* declaration)
    {
        if (isRenameableDeclaration(declaration))
        {
            protect(declaration, declaration->getLocation());
        }
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr* expression)
    {
        protect(expression->getDecl(), expression->getLocation());
        return true;
    }

    bool VisitMemberExpr(MemberExpr* expression)
    {
        protect(expression->getMemberDecl(), expression->getMemberLoc());
        return true;
    }

    bool VisitTypedefTypeLoc(TypedefTypeLoc location)
    {
        protect(typedefDeclaration(location), location.getNameLoc());
        return true;
    }

    bool VisitTagTypeLoc(TagTypeLoc location)
    {
        protect(location.getDecl(), location.getNameLoc());
        return true;
    }

    bool VisitDesignatedInitExpr(DesignatedInitExpr* expression)
    {
        for (const DesignatedInitExpr::Designator& designator : expression->designators())
        {
            if (designator.isFieldDesignator())
            {
                protect(designator.getFieldDecl(), designator.getFieldLoc());
            }
        }
        return true;
    }

private:
    void protect(const NamedDecl* declaration, SourceLocation location)
    {
        const NamedDecl* canonical = canonicalNamedDecl(declaration);
        const auto       symbol    = symbols_.find(canonical);
        if (symbol != symbols_.end() && editableSpellingLocation(location, sourceManager_).isInvalid())
        {
            symbol->second.renameable = false;
        }
    }

    const SourceManager& sourceManager_;
    SymbolMap&           symbols_;
};

void assignGeneratedNames(SymbolMap& symbols, std::uint32_t seed, std::set<std::string>& reserved)
{
    std::vector<Symbol*> ordered;
    for (auto& entry : symbols)
    {
        if (entry.second.renameable)
        {
            ordered.push_back(&entry.second);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const Symbol* left, const Symbol* right) { return left->declarationOffset < right->declarationOffset; });

    std::uint32_t randomState = seed == 0 ? 0x6d2b79f5u : seed;
    for (Symbol* symbol : ordered)
    {
        symbol->replacement = makeUniqueName(randomState, reserved);
    }
}

void protectAmbiguousSkippedSymbols(SymbolMap& symbols, const FrontendState& state)
{
    std::set<std::string> skippedSpellings;
    for (const auto& occurrence : state.skippedIdentifiers)
    {
        skippedSpellings.insert(occurrence.second.spelling);
    }

    std::map<std::string, std::vector<Symbol*>> symbolsBySpelling;
    for (auto& entry : symbols)
    {
        Symbol& symbol = entry.second;
        if (skippedSpellings.find(symbol.spelling) != skippedSpellings.end())
        {
            symbolsBySpelling[symbol.spelling].push_back(&symbol);
        }
    }

    for (auto& entry : symbolsBySpelling)
    {
        if (entry.second.size() != 1)
        {
            for (Symbol* symbol : entry.second)
            {
                symbol->renameable = false;
            }
        }
    }
}

void addSkippedIdentifierReplacements(const SourceManager& sourceManager, const SymbolMap& symbols, FrontendState& state)
{
    std::map<std::string, const Symbol*> uniqueSymbols;
    std::set<std::string>                ambiguousSpellings;
    for (const auto& entry : symbols)
    {
        const Symbol& symbol = entry.second;
        if (ambiguousSpellings.find(symbol.spelling) != ambiguousSpellings.end())
        {
            continue;
        }

        const auto inserted = uniqueSymbols.emplace(symbol.spelling, &symbol);
        if (!inserted.second)
        {
            uniqueSymbols.erase(inserted.first);
            ambiguousSpellings.insert(symbol.spelling);
        }
    }

    const SourceLocation fileStart = sourceManager.getLocForStartOfFile(sourceManager.getMainFileID());
    for (const auto& occurrence : state.skippedIdentifiers)
    {
        const auto symbol = uniqueSymbols.find(occurrence.second.spelling);
        if (symbol == uniqueSymbols.end() || symbol->second->replacement.empty())
        {
            continue;
        }

        if (occurrence.first > static_cast<unsigned int>(std::numeric_limits<int>::max()))
        {
            state.internalError =
                "LibTooling skipped-branch offset exceeds the supported "
                "source range";
            return;
        }

        const SourceLocation        location = fileStart.getLocWithOffset(static_cast<int>(occurrence.first));
        clang::tooling::Replacement replacement(sourceManager, location, occurrence.second.length, symbol->second->replacement);
        if (!replacement.isApplicable())
        {
            state.internalError =
                "LibTooling produced an inapplicable skipped-branch "
                "replacement";
            return;
        }
        if (llvm::Error error = state.replacements.add(replacement))
        {
            state.internalError =
                "LibTooling produced conflicting skipped-branch "
                "replacements: " +
                llvm::toString(std::move(error));
            return;
        }
    }
}

class ReplacementCollector final : public clang::RecursiveASTVisitor<ReplacementCollector>
{
public:
    ReplacementCollector(ASTContext& context, const SymbolMap& symbols, FrontendState& state)
        : sourceManager_(context.getSourceManager()), languageOptions_(context.getLangOpts()), symbols_(symbols), state_(state), opaqueIndex_(0)
    {}

    bool VisitNamedDecl(NamedDecl* declaration)
    {
        if (isRenameableDeclaration(declaration))
        {
            addSymbolReplacement(declaration, declaration->getLocation());
        }
        return !hasFailed();
    }

    bool VisitDeclRefExpr(DeclRefExpr* expression)
    {
        addSymbolReplacement(expression->getDecl(), expression->getLocation());
        return !hasFailed();
    }

    bool VisitMemberExpr(MemberExpr* expression)
    {
        addSymbolReplacement(expression->getMemberDecl(), expression->getMemberLoc());
        return !hasFailed();
    }

    bool VisitTypedefTypeLoc(TypedefTypeLoc location)
    {
        addSymbolReplacement(typedefDeclaration(location), location.getNameLoc());
        return !hasFailed();
    }

    bool VisitTagTypeLoc(TagTypeLoc location)
    {
        addSymbolReplacement(location.getDecl(), location.getNameLoc());
        return !hasFailed();
    }

    bool VisitDesignatedInitExpr(DesignatedInitExpr* expression)
    {
        for (const DesignatedInitExpr::Designator& designator : expression->designators())
        {
            if (designator.isFieldDesignator())
            {
                addSymbolReplacement(designator.getFieldDecl(), designator.getFieldLoc());
            }
        }
        return !hasFailed();
    }

    bool VisitIfStmt(IfStmt* statement)
    {
        if (!state_.options.insertOpaquePredicates || hasFailed())
        {
            return !hasFailed();
        }

        const auto* body = llvm::dyn_cast_or_null<clang::CompoundStmt>(statement->getThen());
        if (body == nullptr)
        {
            return true;
        }

        const SourceLocation brace = editableSpellingLocation(body->getLBracLoc(), sourceManager_);
        if (brace.isInvalid())
        {
            return true;
        }

        const unsigned int tokenLength = clang::Lexer::MeasureTokenLength(brace, sourceManager_, languageOptions_);
        if (tokenLength == 0)
        {
            return true;
        }

        const SourceLocation insertion = brace.getLocWithOffset(tokenLength);
        addReplacement(insertion, 0, opaquePredicate(state_.options.seed == 0 ? 0x6d2b79f5u : state_.options.seed, opaqueIndex_++));
        return !hasFailed();
    }

private:
    void addSymbolReplacement(const NamedDecl* declaration, SourceLocation location)
    {
        if (declaration == nullptr || hasFailed())
        {
            return;
        }

        const auto symbol = symbols_.find(canonicalNamedDecl(declaration));
        if (symbol == symbols_.end() || symbol->second.replacement.empty())
        {
            return;
        }

        const SourceLocation spelling = editableSpellingLocation(location, sourceManager_);
        if (spelling.isInvalid())
        {
            return;
        }

        const unsigned int length = clang::Lexer::MeasureTokenLength(spelling, sourceManager_, languageOptions_);
        if (length == 0)
        {
            return;
        }
        addReplacement(spelling, length, symbol->second.replacement);
    }

    void addReplacement(SourceLocation location, unsigned int length, const std::string& replacementText)
    {
        clang::tooling::Replacement replacement(sourceManager_, location, length, replacementText);
        if (!replacement.isApplicable())
        {
            state_.internalError = "LibTooling produced an inapplicable source replacement";
            return;
        }

        if (llvm::Error error = state_.replacements.add(replacement))
        {
            state_.internalError = "LibTooling produced conflicting source replacements: " + llvm::toString(std::move(error));
        }
    }

    bool hasFailed() const { return !state_.internalError.empty(); }

    const SourceManager& sourceManager_;
    const LangOptions&   languageOptions_;
    const SymbolMap&     symbols_;
    FrontendState&       state_;
    std::uint32_t        opaqueIndex_;
};

class SemanticASTConsumer final : public clang::ASTConsumer
{
public:
    explicit SemanticASTConsumer(FrontendState& state) : state_(state) {}

    void HandleTranslationUnit(ASTContext& context) override
    {
        if (context.getDiagnostics().hasErrorOccurred())
        {
            return;
        }

        if (!state_.options.applyTransformations)
        {
            state_.transformedSource = state_.source;
            state_.completed         = true;
            return;
        }

        SymbolCollector collector(context, state_);
        collector.TraverseDecl(context.getTranslationUnitDecl());
        SymbolMap symbols = collector.takeSymbols();

        ReferenceProtector protector(context.getSourceManager(), symbols);
        protector.TraverseDecl(context.getTranslationUnitDecl());

        protectAmbiguousSkippedSymbols(symbols, state_);

        std::set<std::string> reserved = collectSourceIdentifiers(state_.source);
        assignGeneratedNames(symbols, state_.options.seed, reserved);

        ReplacementCollector replacements(context, symbols, state_);
        replacements.TraverseDecl(context.getTranslationUnitDecl());
        if (!state_.internalError.empty())
        {
            return;
        }

        addSkippedIdentifierReplacements(context.getSourceManager(), symbols, state_);
        if (!state_.internalError.empty())
        {
            return;
        }

        llvm::Expected<std::string> transformed = clang::tooling::applyAllReplacements(state_.source, state_.replacements);
        if (!transformed)
        {
            state_.internalError = "LibTooling could not apply source replacements: " + llvm::toString(transformed.takeError());
            return;
        }

        state_.transformedSource = std::move(*transformed);
        state_.completed         = true;
    }

private:
    FrontendState& state_;
};

class SemanticFrontendAction final : public clang::ASTFrontendAction
{
public:
    explicit SemanticFrontendAction(FrontendState& state) : state_(state) {}

    bool BeginSourceFileAction(clang::CompilerInstance& compiler) override
    {
        compiler.getPreprocessor().addPPCallbacks(std::make_unique<PreprocessorTracker>(compiler.getSourceManager(), compiler.getLangOpts(), state_));
        return true;
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override { return std::make_unique<SemanticASTConsumer>(state_); }

private:
    FrontendState& state_;
};

class SemanticFrontendActionFactory final : public clang::tooling::FrontendActionFactory
{
public:
    explicit SemanticFrontendActionFactory(FrontendState& state) : state_(state) {}

    std::unique_ptr<clang::FrontendAction> create() override { return std::make_unique<SemanticFrontendAction>(state_); }

private:
    FrontendState& state_;
};

const char* diagnosticLevelName(clang::DiagnosticsEngine::Level level)
{
    switch (level)
    {
    case clang::DiagnosticsEngine::Ignored:
        return "ignored";
    case clang::DiagnosticsEngine::Note:
        return "note";
    case clang::DiagnosticsEngine::Remark:
        return "remark";
    case clang::DiagnosticsEngine::Warning:
        return "warning";
    case clang::DiagnosticsEngine::Error:
        return "error";
    case clang::DiagnosticsEngine::Fatal:
        return "fatal error";
    }
    return "diagnostic";
}

class CapturingDiagnosticConsumer final : public DiagnosticConsumer
{
public:
    CapturingDiagnosticConsumer(const std::string& inputPath, const std::string& source) : inputPath_(inputPath), source_(source) {}

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const Diagnostic& information) override
    {
        DiagnosticConsumer::HandleDiagnostic(level, information);
        if (level >= clang::DiagnosticsEngine::Error)
        {
            hasErrors_ = true;
        }
        if (level == clang::DiagnosticsEngine::Ignored)
        {
            return;
        }

        llvm::SmallString<256> message;
        information.FormatDiagnostic(message);

        const SourceLocation location = information.getLocation();
        if (location.isValid() && information.hasSourceManager())
        {
            const SourceManager& sourceManager = information.getSourceManager();
            const SourceLocation spelling      = sourceManager.getSpellingLoc(location);
            const unsigned int   line          = sourceManager.getSpellingLineNumber(spelling);
            const unsigned int   column        = sourceManager.getSpellingColumnNumber(spelling);
            const bool           inMainFile    = sourceManager.isWrittenInMainFile(spelling);

            if (inMainFile)
            {
                output_ << inputPath_ << ':' << line << ':' << column << ": ";
            }
            else
            {
                output_ << sourceManager.getFilename(spelling).str() << ':' << line << ':' << column << ": ";
            }
            output_ << diagnosticLevelName(level) << ": " << message.str().str() << '\n';

            if (inMainFile)
            {
                appendSourceLine(line, column);
            }
        }
        else
        {
            output_ << diagnosticLevelName(level) << ": " << message.str().str() << '\n';
        }
    }

    bool hasErrors() const { return hasErrors_; }

    std::string str() const { return output_.str(); }

private:
    void appendSourceLine(unsigned int line, unsigned int column)
    {
        if (line == 0)
        {
            return;
        }

        std::size_t lineStart = 0;
        for (unsigned int currentLine = 1; currentLine < line && lineStart < source_.size(); ++currentLine)
        {
            const std::size_t newline = source_.find('\n', lineStart);
            if (newline == std::string::npos)
            {
                return;
            }
            lineStart = newline + 1;
        }

        std::size_t lineEnd = source_.find('\n', lineStart);
        if (lineEnd == std::string::npos)
        {
            lineEnd = source_.size();
        }
        if (lineEnd > lineStart && source_[lineEnd - 1] == '\r')
        {
            --lineEnd;
        }

        output_ << "    " << source_.substr(lineStart, lineEnd - lineStart) << '\n' << "    ";
        const unsigned int caretColumn = column == 0 ? 1 : column;
        for (unsigned int index = 1; index < caretColumn; ++index)
        {
            const std::size_t sourceIndex = lineStart + index - 1;
            output_ << (sourceIndex < source_.size() && source_[sourceIndex] == '\t' ? '\t' : ' ');
        }
        output_ << "^\n";
    }

    const std::string& inputPath_;
    const std::string& source_;
    std::ostringstream output_;
    bool               hasErrors_ = false;
};

std::string absolutePath(const std::string& path)
{
    llvm::SmallString<256> absolute(path);
    if (llvm::sys::fs::make_absolute(absolute))
    {
        return path;
    }
    llvm::sys::path::remove_dots(absolute, true);
    return absolute.str().str();
}
}  // namespace

bool HasLibToolingFrontend()
{
    return true;
}

LibToolingFrontendResult RunLibToolingFrontend(const std::string& inputPath, const std::string& source, const LibToolingFrontendOptions& options)
{
    LibToolingFrontendResult result;
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()))
    {
        result.diagnostics = "The input is too large for LibTooling source offsets.";
        return result;
    }

    const std::string      canonicalInputPath = absolutePath(inputPath);
    llvm::SmallString<256> workingDirectory(canonicalInputPath);
    llvm::sys::path::remove_filename(workingDirectory);
    if (workingDirectory.empty())
    {
        workingDirectory = ".";
    }

    std::vector<std::string> arguments{
        "-x",
        "cl",
        "-cl-std=CL1.2",
        "-trigraphs",
        "-resource-dir=" OPEN_SLEX_CLANG_RESOURCE_DIR,
        "-Xclang",
        "-finclude-default-header",
        "-Wno-trigraphs",
        "-Werror=unknown-escape-sequence",
        "-fsyntax-only",
    };
    arguments.push_back("-I" + llvm::StringRef(workingDirectory).str());
    arguments.insert(arguments.end(), options.compilerArguments.begin(), options.compilerArguments.end());

    FrontendState                            state(canonicalInputPath, source, options);
    CapturingDiagnosticConsumer              diagnostics(canonicalInputPath, source);
    clang::tooling::FixedCompilationDatabase compilationDatabase(llvm::StringRef(workingDirectory), arguments);
    clang::tooling::ClangTool                tool(compilationDatabase, { canonicalInputPath });
    tool.mapVirtualFile(canonicalInputPath, source);
    tool.setDiagnosticConsumer(&diagnostics);

    SemanticFrontendActionFactory factory(state);
    const int                     toolResult = tool.run(&factory);
    result.diagnostics                       = diagnostics.str();

    if (diagnostics.hasErrors())
    {
        result.status = LibToolingFrontendStatus::SyntaxError;
        return result;
    }
    if (!state.internalError.empty())
    {
        result.diagnostics += state.internalError + ".\n";
        return result;
    }
    if (toolResult != 0 || !state.completed)
    {
        result.diagnostics += "LibTooling could not complete the semantic frontend action.\n";
        return result;
    }

    result.transformedSource = std::move(state.transformedSource);
    result.status            = LibToolingFrontendStatus::Success;
    return result;
}

#else

bool HasLibToolingFrontend()
{
    return false;
}

LibToolingFrontendResult RunLibToolingFrontend(const std::string&, const std::string&, const LibToolingFrontendOptions&)
{
    LibToolingFrontendResult result;
    result.diagnostics = "This OpenSLex build does not contain the LibTooling frontend.";
    return result;
}

#endif

LibToolingFrontendResult ValidateOpenCLSource(const std::string& inputPath, const std::string& source, const std::vector<std::string>& compilerArguments)
{
    LibToolingFrontendOptions options;
    options.compilerArguments      = compilerArguments;
    options.insertOpaquePredicates = false;
    options.applyTransformations   = false;
    return RunLibToolingFrontend(inputPath, source, options);
}
