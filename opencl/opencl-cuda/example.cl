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
grayscale(__global uchar4* input,
          __global uchar* output,
          const unsigned int numRows,
          const unsigned int numCols)
{
    int xind = get_global_id(0);
    int yind = get_global_id(1);
    if (xind < numRows && yind < numCols)
    {
        int ind = xind * numCols + yind;
        uchar R = input[ind].x;
        uchar G = input[ind].y;
        uchar B = input[ind].z;
        // output = .299f * R + .587f * G + .114f * B
        output[ind] = (uchar)(.299f * R + .587f * G + .114f * B);
    }
}
