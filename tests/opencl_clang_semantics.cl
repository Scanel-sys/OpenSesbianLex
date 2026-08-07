#define CALL_MACRO_TARGET(argument) macro_target(argument)
#define OPEN_SLEX_KERNEL __kernel
#include "clang_macro_header.h"

typedef struct
{
    int value;
    float4 position;
} SemanticItem;

int macro_target(int argument)
{
    return argument + 1;
}

int semantic_external_helper(int argument)
{
    return argument + 2;
}

int semantic_helper(SemanticItem* item)
{
    int value = 11;
    int x = 13;
    float component = item->position.x;
    return item->value + value + x + (int)component;
}

__kernel void clang_semantic_frontend(__global int* output)
{
    SemanticItem item = {
        .value = 5,
        .position = (float4)(1.0f, 2.0f, 3.0f, 4.0f),
    };
    float2 xy = item.position.xy;

    if (xy.s0 > 0.0f)
    {
        output[0] = CALL_EXTERNAL_HELPER(
            CALL_MACRO_TARGET(semantic_helper(&item)));
    }
}

OPEN_SLEX_KERNEL void macro_qualified_kernel(__global int* output)
{
    output[1] = 17;
}
