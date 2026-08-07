#pragma once

#include <string>

bool WriteFileAtomically(
    const std::string& outputPath,
    const std::string& contents,
    std::string& errorMessage);
