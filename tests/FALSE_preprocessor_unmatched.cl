#else
__kernel void unmatched_else(__global int* output)
{
    output[0] = 1;
}
