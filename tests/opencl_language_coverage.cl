#define ROTATE_AND_SELECT(value, amount, fallback) \
    ((((value) << (amount)) | ((value) >> (32U - (amount)))) != 0U \
        ? (value) \
        : (fallback))

typedef enum
{
    MODE_COPY = 0,
    MODE_ACCUMULATE = 1,
} OperationMode;

typedef struct
{
    float4 position;
    float4 field;
    int value;
} Particle;

struct TaggedParticle
{
    float4 color;
};

int condition_with_side_effect(__global volatile uint* counter)
{
    return atomic_inc(counter) == 0U;
}

__kernel __attribute__((reqd_work_group_size(1, 1, 1), vec_type_hint(int4)))
void language_coverage(
    __global volatile uint* counter,
    __global int* restrict output)
{
    Particle particles[2];
    struct TaggedParticle tagged_particle;
    int x = 1;
    int y = 2;
    int z = 3;
    int M = 4;
    int N = 5;
    int value = 6;
    int index = 0;
    int4 lanes = (int4)(1, 2, 3, 4);
    float scalar = (float)value;
    ulong mask = 0xFF00FF00UL;
    uint shifted = ((uint)value << 2U) | ((uint)value >> 1U);
    char letter = 'a';
    OperationMode mode = MODE_ACCUMULATE;

    particles[index].position = (float4)(scalar, 2.0f, 3.0f, 4.0f);
    particles[index].field = (float4)(5.0f, 6.0f, 7.0f, 8.0f);
    particles[index].value = value;
    tagged_particle.color = particles[index].field;
    float2 xy = particles[index].field.xy;
    float component = particles[index].position.x;

    if (condition_with_side_effect(counter) && counter[0] != 7U)
    {
        output[0] = (int)(xy.x + component);
    }

    switch (mode)
    {
        case MODE_COPY:
            output[1] = particles[index].value;
            break;
        case MODE_ACCUMULATE:
            output[1] = x + y + z + M + N + value;
            break;
        default:
            output[1] = -1;
            break;
    }

    output[2] = (int)(ROTATE_AND_SELECT(shifted, 3U, 9U) ^ (uint)mask);
    output[3] = (int)sizeof(Particle) + (int)letter + lanes.s0;

#ifdef cl_khr_fp16
    half half_value = (half)1.0h;
    output[4] = (int)half_value;
#else
    output[4] = 1;
#endif
}

kernel void image_language_coverage(
    read_only image2d_t input_image,
    sampler_t image_sampler,
    write_only image2d_t output_image)
{
    int2 coordinate = (int2)(get_global_id(0), get_global_id(1));
    float4 pixel = read_imagef(input_image, image_sampler, coordinate);
    write_imagef(output_image, coordinate, pixel);
}
