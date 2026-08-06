__kernel void invalid_string_newline()
{
    printf("an unescaped newline
inside a string");
}
