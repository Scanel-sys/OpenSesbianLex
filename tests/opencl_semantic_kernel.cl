#define ENABLE_FEATURE_PATH 1
#define SCALE_VALUE(value) ((value) * 2)

#ifdef ENABLE_FEATURE_PATH
typedef int feature_int;
#endif

typedef struct
{
    int first;
    int second;
} Pair;

__kernel void semantic_kernel(
    __global const int* input,
    __global int* output,
    __local int* scratch)
{
    size_t index = get_global_id(0);
    uint local_index = get_local_id(0);
    int4 lanes = vload4(index, input);
    int2 selected = lanes.xy;

    Pair pair;
    Pair* pair_pointer = &pair;
    pair.first = lanes.s0;
    pair_pointer->second = selected.s1;

    int outer_value = pair_pointer->first + pair.second;
    scratch[local_index] = outer_value;
    barrier(CLK_LOCAL_MEM_FENCE);

    feature_int transformed =
        SCALE_VALUE(scratch[1 - local_index]);

    for (int iteration = 0; iteration < 2; iteration++)
    {
        transformed += iteration;
    }

    {
        int outer_value = 5;
        transformed += outer_value;
    }

    if (lanes.s0 < 10)
    {
        output[index] = transformed + 1;
    }
    else
    {
        output[index] = transformed - 1;
    }
}
