#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ClangFrontendOptions
{
    std::uint32_t seed = 0x9e3779b9u;
    std::vector<std::string> compilerArguments;
    bool insertOpaquePredicates = true;
};

enum class ClangFrontendStatus
{
    Success,
    SyntaxError,
    FrontendError,
};

struct ClangFrontendResult
{
    ClangFrontendStatus status = ClangFrontendStatus::FrontendError;
    std::string transformedSource;
    std::string diagnostics;
};

bool HasClangSemanticFrontend();

ClangFrontendResult RunClangSemanticFrontend(
    const std::string& inputPath,
    const std::string& source,
    const ClangFrontendOptions& options);
