#define KEEP_MACRO(value) ((value) + 1)

typedef struct
{
    int x;
} Record_t;

int helper_value(int x)
{
    return x + 1;
}

__kernel void symbol_scopes(__global int* output)
{
    int x = 1;
    int UPPER_LOCAL = 2;
    int value_t = 3;
    int4 lanes = vload4(0, output);

    Record_t record;
    record.x = x;
    Record_t const constant_record = record;
    output[0] = KEEP_MACRO(
        constant_record.x + lanes.x + UPPER_LOCAL + value_t);

    {
        int x = 7;
        output[1] = helper_value(x);
    }

    barrier(CLK_LOCAL_MEM_FENCE);
}
