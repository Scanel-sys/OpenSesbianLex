#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LibToolingFrontendOptions
{
    std::uint32_t            seed = 0x9e3779b9u;
    std::vector<std::string> compilerArguments;
    bool                     insertOpaquePredicates = true;
    bool                     applyTransformations   = true;
};

enum class LibToolingFrontendStatus {
    Success,
    SyntaxError,
    FrontendError,
};

struct LibToolingFrontendResult
{
    LibToolingFrontendStatus status = LibToolingFrontendStatus::FrontendError;
    std::string              transformedSource;
    std::string              diagnostics;
};

bool HasLibToolingFrontend();

LibToolingFrontendResult RunLibToolingFrontend(const std::string& inputPath, const std::string& source, const LibToolingFrontendOptions& options);

LibToolingFrontendResult ValidateOpenCLSource(const std::string& inputPath, const std::string& source, const std::vector<std::string>& compilerArguments);
