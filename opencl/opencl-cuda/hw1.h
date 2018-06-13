//
//  hw1.h
//  opencl-cuda-problem-set-1
//
//  Created by Kan Xu on 6/12/18.
//  Copyright © 2018 Kan Xu. All rights reserved.
//

#ifndef hw1_h
#define hw1_h

#include "cuda-struct.h"

struct RawImage
{
public:
    RawImage()
    : width(0)
    , height(0)
    , data(nullptr)
    {}
    
    int width;
    int height;
    const uchar4* data;
};

//return types are void since any internal error will be handled by quitting
//no point in returning error codes...
//returns a pointer to an RGBA version of the input image
//and a pointer to the single channel grey-scale output
//on both the host and device
void loadImage(const std::string &filename, RawImage* outImage);

void postProcess(const std::string& output_file, int numRows, int numCols, unsigned char* data_ptr);

#endif /* hw1_h */
