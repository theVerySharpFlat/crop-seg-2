#pragma once

#include <cassert>
#include <csignal>
#include <cstddef>
#include <cstdio>

#define gpuErrchk(ans)                                                         \
  {                                                                            \
    gpuAssert((ans), __FILE__, __LINE__);                                      \
  }

#define gpuErrchkPassthrough(ans)                                              \
  {                                                                            \
    gpuAssert((ans), __FILE__, __LINE__, false);                               \
  }

inline void gpuAssert(cudaError_t code, const char *file, int line,
                      bool abort = true) {
  if (code != cudaSuccess) {
    fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file,
            line);
    if (abort)
      raise(SIGTRAP);
  }
}

__global__ void JoinUCharMasks(cudaPitchedPtr masks, cudaPitchedPtr outMask,
                               size_t bandDimX, size_t bandDimY, size_t nMasks,
                               unsigned char boundMin = 1,
                               unsigned char boundMax = 255);

// void joinDetfooMasks(unsigned char *masks, unsigned char *outMask,
//                      size_t bandDimX, size_t bandDimY, size_t nMasks);
void joinMasks(unsigned char *detfooMasks, size_t nDetfooMasks,
               unsigned char *cldMask, unsigned char maxCldPercentage,
               unsigned char *snwMask, unsigned char maxSnwPercentage,
               unsigned char *outMask, size_t bandDimX, size_t bandDimY);
