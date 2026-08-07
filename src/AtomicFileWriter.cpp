#include "AtomicFileWriter.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
constexpr unsigned int maxTempFileAttempts = 100;

unsigned long currentProcessId()
{
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

std::string makeTempFilePath(
    const std::string& outputPath,
    unsigned int attempt)
{
    return outputPath + ".openslex.tmp." +
        std::to_string(currentProcessId()) + "." + std::to_string(attempt);
}

#ifdef _WIN32
std::string windowsErrorMessage(const char* operation, DWORD errorCode)
{
    return std::string(operation) + " failed with Windows error " +
        std::to_string(static_cast<unsigned long>(errorCode));
}
#endif
}

bool WriteFileAtomically(
    const std::string& outputPath,
    const std::string& contents,
    std::string& errorMessage)
{
    for (unsigned int attempt = 0; attempt < maxTempFileAttempts; ++attempt)
    {
        const std::string tempPath = makeTempFilePath(outputPath, attempt);

#ifdef _WIN32
        HANDLE tempFile = CreateFileA(
            tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (tempFile == INVALID_HANDLE_VALUE)
        {
            const DWORD errorCode = GetLastError();
            if (errorCode == ERROR_FILE_EXISTS ||
                errorCode == ERROR_ALREADY_EXISTS)
            {
                continue;
            }

            errorMessage = windowsErrorMessage(
                "creating temporary output file", errorCode);
            return false;
        }

        bool writeSucceeded = true;
        std::size_t writtenTotal = 0;
        while (writtenTotal < contents.size())
        {
            const std::size_t remaining = contents.size() - writtenTotal;
            const DWORD chunkSize = remaining >
                    static_cast<std::size_t>(
                        std::numeric_limits<DWORD>::max())
                ? std::numeric_limits<DWORD>::max()
                : static_cast<DWORD>(remaining);
            DWORD writtenNow = 0;
            if (WriteFile(
                    tempFile, contents.data() + writtenTotal, chunkSize,
                    &writtenNow, nullptr) == 0 ||
                writtenNow == 0)
            {
                errorMessage = windowsErrorMessage(
                    "writing temporary output file", GetLastError());
                writeSucceeded = false;
                break;
            }
            writtenTotal += writtenNow;
        }

        if (writeSucceeded && FlushFileBuffers(tempFile) == 0)
        {
            errorMessage = windowsErrorMessage(
                "flushing temporary output file", GetLastError());
            writeSucceeded = false;
        }

        if (CloseHandle(tempFile) == 0 && writeSucceeded)
        {
            errorMessage = windowsErrorMessage(
                "closing temporary output file", GetLastError());
            writeSucceeded = false;
        }

        if (!writeSucceeded)
        {
            DeleteFileA(tempPath.c_str());
            return false;
        }

        if (MoveFileExA(
                tempPath.c_str(), outputPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            errorMessage = windowsErrorMessage(
                "replacing output file", GetLastError());
            DeleteFileA(tempPath.c_str());
            return false;
        }
#else
        const int tempFile = open(
            tempPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (tempFile == -1)
        {
            if (errno == EEXIST)
            {
                continue;
            }

            errorMessage = std::string("creating temporary output file failed: ") +
                std::strerror(errno);
            return false;
        }

        bool writeSucceeded = true;
        std::size_t writtenTotal = 0;
        while (writtenTotal < contents.size())
        {
            const ssize_t writtenNow = write(
                tempFile, contents.data() + writtenTotal,
                contents.size() - writtenTotal);
            if (writtenNow == -1 && errno == EINTR)
            {
                continue;
            }
            if (writtenNow <= 0)
            {
                errorMessage = std::string(
                    "writing temporary output file failed: ") +
                    std::strerror(errno);
                writeSucceeded = false;
                break;
            }
            writtenTotal += static_cast<std::size_t>(writtenNow);
        }

        while (writeSucceeded && fsync(tempFile) == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            errorMessage = std::string(
                "flushing temporary output file failed: ") +
                std::strerror(errno);
            writeSucceeded = false;
        }

        if (close(tempFile) == -1 && writeSucceeded)
        {
            errorMessage = std::string(
                "closing temporary output file failed: ") +
                std::strerror(errno);
            writeSucceeded = false;
        }

        if (!writeSucceeded)
        {
            unlink(tempPath.c_str());
            return false;
        }

        if (std::rename(tempPath.c_str(), outputPath.c_str()) == -1)
        {
            errorMessage = std::string("replacing output file failed: ") +
                std::strerror(errno);
            unlink(tempPath.c_str());
            return false;
        }
#endif

        return true;
    }

    errorMessage = "could not allocate a unique temporary output file";
    return false;
}
