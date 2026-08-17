__kernel void libtooling_rejects_undeclared(__global int* output)
{
    output[0] = this_identifier_was_never_declared;
}
