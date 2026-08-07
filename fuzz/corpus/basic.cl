__kernel void basic(__global int* output)
{
    int4 lanes = (int4)(1, 2, 3, 4);
    output[0] = lanes.s0;
}
