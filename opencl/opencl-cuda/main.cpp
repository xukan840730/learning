//
//  main.cpp
//  opencl-cuda-problem-set-1
//
//  Created by Kan Xu on 6/10/18.
//  Copyright © 2018 Kan Xu. All rights reserved.
//

#include <iostream>
#include "cuda-struct.h"
#include "hw1.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <OpenCL/opencl.h>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>

static char *
load_program_source(const char *filename)
{
    struct stat statbuf;
    FILE        *fh;
    char        *source;
    
    fh = fopen(filename, "r");
    if (fh == 0)
        return 0;
    
    stat(filename, &statbuf);
    source = (char *) malloc(statbuf.st_size + 1);
    fread(source, statbuf.st_size, 1, fh);
    source[statbuf.st_size] = '\0';
    
    return source;
}

int main(int argc, const char * argv[]) {
    
    std::string input_file;
    std::string output_file;
    std::string reference_file;
    double perPixelError = 0.0;
    double globalError   = 0.0;
    bool useEpsCheck = false;
    switch (argc)
    {
        case 2:
            input_file = std::string(argv[1]);
            output_file = "HW1_output.png";
            reference_file = "HW1_reference.png";
            break;
        case 3:
            input_file  = std::string(argv[1]);
            output_file = std::string(argv[2]);
            reference_file = "HW1_reference.png";
            break;
        case 4:
            input_file  = std::string(argv[1]);
            output_file = std::string(argv[2]);
            reference_file = std::string(argv[3]);
            break;
        case 6:
            useEpsCheck=true;
            input_file  = std::string(argv[1]);
            output_file = std::string(argv[2]);
            reference_file = std::string(argv[3]);
            perPixelError = atof(argv[4]);
            globalError   = atof(argv[5]);
            break;
        default:
            std::cerr << "Usage: ./HW1 input_file [output_filename] [reference_filename] [perPixelError] [globalError]" << std::endl;
            exit(1);
    }
    
    //load the image and give us our input and output pointers
    RawImage rawImage;
    loadImage(input_file, &rawImage);
    
    cl_device_id device_id;             // compute device id
    cl_context context;                 // compute context
    cl_command_queue commands;          // compute command queue
    cl_program program;                 // compute program
    cl_kernel kernel;                   // compute kernel
    
    // Connect to a compute device
    int gpu = 1;
    int err = clGetDeviceIDs(NULL, gpu ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to create a device group!\n");
        return EXIT_FAILURE;
    }

    // Create a compute context
    context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
    if (!context)
    {
        printf("Error: Failed to create a compute context!\n");
        return EXIT_FAILURE;
    }
    
    // Create a command commands
    commands = clCreateCommandQueue(context, device_id, 0, &err);
    if (!commands)
    {
        printf("Error: Failed to create a command commands!\n");
        return EXIT_FAILURE;
    }
    
    const char* filename = "example.cl";
    // Load the compute program from disk into a cstring buffer
    char *source = load_program_source(filename);
    if (!source)
    {
        printf("Error: Failed to load compute program from file!\n");
        return EXIT_FAILURE;
    }
    
    // Create the compute program from the source buffer
    //
    program = clCreateProgramWithSource(context, 1, (const char **) & source, NULL, &err);
    if (!program)
    {
        printf("Error: Failed to create compute program!\n");
        return EXIT_FAILURE;
    }
    
    // Build the program executable
    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        size_t len;
        char buffer[2048];
        
        printf("Error: Failed to build program executable!\n");
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        printf("%s\n", buffer);
        exit(1);
    }
    
    // Create the compute kernel in the program we wish to run
    kernel = clCreateKernel(program, "grayscale", &err);
    if (!kernel || err != CL_SUCCESS)
    {
        printf("Error: Failed to create compute kernel!\n");
        exit(1);
    }
    
    const size_t numPixels = rawImage.width * rawImage.height;
    
    // Create the input and output arrays in device memory for our calculation
    cl_mem input;                       // device memory used for the input array
    cl_mem output;                      // device memory used for the output array
    input = clCreateBuffer(context,  CL_MEM_READ_ONLY,  sizeof(uchar4) * numPixels, NULL, NULL);
    output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(uchar) * numPixels, NULL, NULL);
    if (!input || !output)
    {
        printf("Error: Failed to allocate device memory!\n");
        exit(1);
    }
    
    // Write our data set into the input array in device memory
    err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(uchar4) * numPixels, rawImage.data, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to write to source array!\n");
        exit(1);
    }
    
    // Set the arguments to our compute kernel
    err = 0;
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
    err |= clSetKernelArg(kernel, 2, sizeof(unsigned int), &rawImage.width);
    err |= clSetKernelArg(kernel, 3, sizeof(unsigned int), &rawImage.height);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to set kernel arguments! %d\n", err);
        exit(1);
    }
    
    // Get the maximum work group size for executing the kernel on the device
//    err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
//    if (err != CL_SUCCESS)
//    {
//        printf("Error: Failed to retrieve kernel work group info! %d\n", err);
//        exit(1);
//    }
    
    // Execute the kernel over the entire range of our 1d input data set
    // using the maximum number of work group items for this device

    size_t localSize[] = {16, 16};
    size_t globalSize[2];
    globalSize[0] = (rawImage.width + localSize[0] - 1) / localSize[0] * localSize[0];
    globalSize[1] = (rawImage.height + localSize[1] - 1) / localSize[1] * localSize[1];
    
    err = clEnqueueNDRangeKernel(commands, kernel, 2, NULL, globalSize, localSize, 0, NULL, NULL);
    if (err)
    {
        printf("Error: Failed to execute kernel!\n");
        return EXIT_FAILURE;
    }
    
    clFinish(commands);
    
    // Read back the results from the device to verify the output
    uchar* results = new uchar[numPixels];
    err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(uchar) * numPixels, results, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to read output array! %d\n", err);
        exit(1);
    }
    
    clFinish(commands);
    
    postProcess(output_file, rawImage.width, rawImage.height, results);

    // Shutdown and cleanup
    clReleaseMemObject(input);
    clReleaseMemObject(output);
    clReleaseProgram(program);
    clReleaseKernel(kernel);
    clReleaseCommandQueue(commands);
    clReleaseContext(context);
    
    return 0;
}
