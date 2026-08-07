#pragma OPENCL FP_CONTRACT ON

#define FEATURE_LEVEL 2
#define TOKEN_JOIN_IMPL(left, right) left ## right
#define STRINGIZE_IMPL(value) #value
#define APPLY(operation, ...) operation(__VA_ARGS__)
#define ADD_VALUES(left, right) ((left) + (right))
#define CALL_HELPER(value) macro_helper(value)
#define MULTILINE_VALUE(value) \
    ((value) + \
     1)
#define COMPLEX_VALUE \
    ((FEATURE_LEVEL << 2) | \
     (FEATURE_LEVEL > 1 ? 2 : 1))

#if defined(FEATURE_LEVEL) && (FEATURE_LEVEL >= 2)
    #define ACTIVE_VALUE 4
#elif FEATURE_LEVEL == 1
    #define ACTIVE_VALUE 2
#else
    #define ACTIVE_VALUE 1
#endif

#if defined(FEATURE_LEVEL)
    #if FEATURE_LEVEL > 0
        #define NESTED_VALUE 1
    #else
        #define NESTED_VALUE 0
    #endif
#endif

#define TEMP_MACRO 1
#undef TEMP_MACRO

int macro_helper(int value)
{
    return value + 1;
}

__kernel void preprocessor_features(__global int* output)
{
    int define = 1;
    int include = 2;
    int ifdef = 3;
    int ifndef = 4;
    int endif = 5;

    #if FEATURE_LEVEL > 1
        output[0] = APPLY(ADD_VALUES, define, include)
            + CALL_HELPER(MULTILINE_VALUE(ifdef))
            + COMPLEX_VALUE + ACTIVE_VALUE + NESTED_VALUE + ifndef + endif;
    #elif FEATURE_LEVEL == 1
        output[0] = define;
    #else
        output[0] = include;
    #endif
}
