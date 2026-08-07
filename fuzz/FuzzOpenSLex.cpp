#include "TextHandler.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;

YY_BUFFER_STATE yy_scan_bytes(const char* bytes, int length);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
int yylex_destroy();

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    std::size_t size)
{
    if (data == nullptr ||
        size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return 0;
    }

    ResetOpenSLexFrontendForFuzzing();
    YY_BUFFER_STATE buffer = yy_scan_bytes(
        reinterpret_cast<const char*>(data),
        static_cast<int>(size));
    if (buffer == nullptr)
    {
        return 0;
    }

    yyparse();
    yy_delete_buffer(buffer);
    yylex_destroy();
    return 0;
}
