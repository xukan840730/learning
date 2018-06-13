
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include "cuda-struct.h"
//#include "utils.h"
//#include <cuda.h>
//#include <cuda_runtime.h>
#include <string>

#include "hw1.h"

cv::Mat imageRGBA;
cv::Mat imageGrey;

//return types are void since any internal error will be handled by quitting
//no point in returning error codes...
//returns a pointer to an RGBA version of the input image
//and a pointer to the single channel grey-scale output
//on both the host and device
void loadImage(const std::string &filename, RawImage* outImage) {
  //make sure the context initializes ok
  //checkCudaErrors(cudaFree(0));

  cv::Mat image;
  image = cv::imread(filename.c_str(), CV_LOAD_IMAGE_COLOR);
  if (image.empty()) {
    std::cerr << "Couldn't open file: " << filename << std::endl;
    exit(1);
  }

  cv::cvtColor(image, imageRGBA, CV_BGR2RGBA);

  //allocate memory for the output
  imageGrey.create(image.rows, image.cols, CV_8UC1);

  //This shouldn't ever happen given the way the images are created
  //at least based upon my limited understanding of OpenCV, but better to check
  if (!imageRGBA.isContinuous() || !imageGrey.isContinuous()) {
    std::cerr << "Images aren't continuous!! Exiting." << std::endl;
    exit(1);
  }

  uchar4* inputImage = (uchar4 *)imageRGBA.ptr<unsigned char>(0);
    outImage->width = imageRGBA.rows;
    outImage->height = imageRGBA.cols;
    outImage->data = inputImage;
}

void postProcess(const std::string& output_file, int numRows, int numCols, unsigned char* data_ptr)
{
  cv::Mat output(numRows, numCols, CV_8UC1, (void*)data_ptr);

  //output the image
  cv::imwrite(output_file.c_str(), output);
}

void cleanup()
{
  //cleanup
  //cudaFree(d_rgbaImage__);
  //cudaFree(d_greyImage__);
}

void generateReferenceImage(std::string input_filename, std::string output_filename)
{
  cv::Mat reference = cv::imread(input_filename, CV_LOAD_IMAGE_GRAYSCALE);

  cv::imwrite(output_filename, reference);
}
