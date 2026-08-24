__kernel void token_boundaries(__global int* output)
{
    int left = 12;
    int right = 3;
    int* pointer = &right;

    output[0] = left + + right;
    output[1] = left - - right;
    output[2] = left / * pointer;
    output[3] = left + ++right;
    output[4] = left - --right;
}
