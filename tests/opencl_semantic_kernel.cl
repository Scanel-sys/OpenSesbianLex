__kernel void semantic_kernel(__global int* values)
{
    size_t index = get_global_id(0);
    int value = values[index];

    if (value < 10)
    {
        values[index] = value * 3 + 1;
    }
    else
    {
        values[index] = value - 2;
    }
}
