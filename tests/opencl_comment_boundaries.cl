__kernel void comment_boundaries(__global int* OUTPUT)
{
    int/**/VALUE = 1;
    int/* first line
           second line */SECOND = 2;
    OUTPUT[0] = VALUE+/**/+SECOND;
}
