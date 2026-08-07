int condition_with_side_effect(
    __global volatile uint* counter,
    __global int* output)
{
    output[1] += 1;
    return counter[0] != 9U;
}

__kernel void opaque_predicate(
    __global volatile uint* counter,
    __global int* output)
{
    if (atomic_inc(counter) == 0U &&
        counter[0] != 7U &&
        condition_with_side_effect(counter, output))
    {
        output[0] = 1;
    }
}
