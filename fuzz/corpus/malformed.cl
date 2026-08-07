__kernel void broken(__global int* output) {
    output[0] = "unterminated
    /* comment
