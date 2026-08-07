__kernel void opaque_predicate(
    __global uint* counter,
    __global int* output)
{
    if (atomic_inc(counter) == 0)
    {
        output[0] = 1;
    }
}
