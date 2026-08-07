#if 1
__kernel void unterminated_if(__global int* output)
{
    output[0] = 1;
}
