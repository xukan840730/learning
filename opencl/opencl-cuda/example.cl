#include "cuda-struct.h"

__kernel void
add(__global float* a,
    __global float* b,
    __global float* answer)
{
    int gid = get_global_id(0);
    answer[gid] = a[gid] + b[gid];
}

__kernel void
square1(__global float* input,
       __global float* output,
       const unsigned int count)
{
    int gid = get_global_id(0);
    if (gid < count)
        output[gid] = input[gid] * input[gid];
}

__kernel void
test(__global uchar4* input,
     __global uchar* output,
     const unsigned int count)
{
    int gid = get_global_id(0);
    if (gid < count)
    {
        uchar R = input[gid].x;
        uchar G = input[gid].y;
        uchar B = input[gid].z;
        // output = .299f * R + .587f * G + .114f * B
        output[gid] = (uchar)(.299f * R + .587f * G + .114f * B);
    }
}
