#include <cassert>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <iostream>

#include "cuda/mapgen.h"
#include "cuda/maskJoin.cuh"

// #define gpuErrchk(ans)                                                         \
//   {                                                                            \
//     gpuAssert((ans), __FILE__, __LINE__);                                      \
//   }
// static inline void gpuAssert(cudaError_t code, const char *file, int line,
//                              bool abort = true) {
//   if (code != cudaSuccess) {
//     fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file,
//             line);
//     if (abort)
//       exit(code);
//   }
// }

__global__ void BuildRowNonzeroSums(cudaPitchedPtr bands, int *out,
                                    size_t out_pitch, size_t bandDimX,
                                    size_t bandDimY, int nBands,
                                    size_t sampleSize) {
  int r = blockDim.x * blockIdx.x + threadIdx.x;
  int c = blockDim.y * blockIdx.y + threadIdx.y;

  int pitch = bands.pitch;
  int slicePitch = bands.pitch * bandDimY;

  int *outRow = (int *)((char *)out + out_pitch * r);

  if ((c > bandDimX - sampleSize) || (r > bandDimY - sampleSize)) {
    if (c < bandDimX && r < bandDimY) {
      outRow[c] = 0;
    }
    return;
  }
  outRow[c] = 0;

  for (int band = 0; band < nBands; band++) {
    unsigned char *bandSlice = (unsigned char *)bands.ptr + band * slicePitch;
    unsigned char *row = (unsigned char *)(bandSlice + r * bands.pitch);

    for (int k = c; k < c + sampleSize; k++) {
      if (row[k] != 0) {
        outRow[c] = outRow[c] + 1;
      }
    }
  }

  // for (int band = 0; band < nBands; band++) {
  //   for (int k = i; k < i + sampleSize; k++) {
  //     out[i][j] = 0;
  //     for (int l = j; l < j + sampleSize; l++) {
  //       out[k][l]++;
  //     }
  //   }
  // }
}

__global__ void BuildSampleMap(int *rowSums, size_t rowSums_pitch,
                               unsigned char *out, size_t out_pitch,
                               int bandDimX, int bandDimY, int sampleSize,
                               float minNonzeroPercentage) {
  int r = blockDim.x * blockIdx.x + threadIdx.x;
  int c = blockDim.y * blockIdx.y + threadIdx.y;

  unsigned char *outRow = (unsigned char *)(out + out_pitch * r);
  if ((c > (bandDimX - sampleSize)) || (r > (bandDimY - sampleSize))) {
    if (c < bandDimX && r < bandDimY) {
      outRow[c] = 0;
    }
    return;
  }

  outRow[c] = 0;

  size_t total = 0;

  for (int k = r; k < r + sampleSize; k++) {
    int *rowSumsRow = (int *)((char *)rowSums + rowSums_pitch * k);

    total += rowSumsRow[c];
  }

  float percentage = (float)total / sampleSize / sampleSize;

  if (percentage > minNonzeroPercentage) {
    outRow[c] = 1;
  }
}

// void buildSampleMap(float ***bands, int **out, int bandDimX, int bandDimY,
//                     int nBands, int sampleSize) {
//   cudaError setErr = cudaSetDevice(0);
//
//   if (setErr == cudaErrorInvalidDevice) {
//     std::cout << "invalid device!" << std::endl;
//   } else if (setErr == cudaErrorDevicesUnavailable) {
//     std::cout << "devices unavailable!" << std::endl;
//   }
//
//   size_t total, free, temp, used;
//   setErr = cudaMemGetInfo(&temp, &total); // get memory information
//
//   // std::cout << "setErr: " << setErr << " " << cudaGetErrorName(setErr)
//   //           << std::endl;
//   //
//   // printf("Total mem: %lu \t free mem before malloc: %lu\n", total,
//   //        temp); // output
//
//   cudaPitchedPtr d_bandsPtr;
//   cudaExtent extent =
//       (make_cudaExtent(bandDimX * sizeof(float), bandDimY, nBands));
//   std::cout << "allocating: " << extent.width * extent.depth * extent.height
//             << std::endl;
//
//   gpuErrchk(cudaMalloc3D(&d_bandsPtr, extent));
//
//   cudaMemcpy3DParms copyParams = {};
//   copyParams.extent = extent;
//   copyParams.kind = cudaMemcpyKind::cudaMemcpyHostToDevice;
//   copyParams.dstPtr = d_bandsPtr;
//   copyParams.srcPtr =
//       make_cudaPitchedPtr(bands, sizeof(float) * bandDimX, bandDimX,
//       bandDimY);
//   gpuErrchk(cudaMemcpy3D(&copyParams));
//
//   int *d_rowSums;
//   size_t d_rowSums_pitch;
//   gpuErrchk(cudaMallocPitch(&d_rowSums, &d_rowSums_pitch,
//                             bandDimX * (sizeof(int)), bandDimY));
//
//   const int threadDim = 16;
//   dim3 threadsPerBlock(threadDim, threadDim);
//   dim3 blocksPerGrid(bandDimX / threadDim + (threadDim - bandDimX %
//   threadDim),
//                      bandDimY / threadDim + (threadDim - bandDimY %
//                      threadDim));
//   BuildRowNonzeroSums<<<blocksPerGrid, threadsPerBlock>>>(
//       d_bandsPtr, d_rowSums, d_rowSums_pitch, bandDimX, bandDimY, nBands,
//       sampleSize);
//   gpuErrchk(cudaPeekAtLastError());
//
//   int *d_out;
//   size_t d_out_pitch;
//   gpuErrchk(cudaMallocPitch(&d_out, &d_out_pitch, bandDimX * (sizeof(int)),
//                             bandDimY));
//   BuildSampleMap<<<blocksPerGrid, threadsPerBlock>>>(
//       d_rowSums, d_rowSums_pitch, d_out, d_out_pitch, bandDimX, bandDimY,
//       sampleSize);
//   gpuErrchk(cudaPeekAtLastError());
// }

namespace sats::cudaproc {
void generateSampleMap(unsigned char *detfooMasks, size_t nDetfooMasks,
                       unsigned char *cldMask, unsigned char maxCldPercentage,
                       unsigned char *snwMask, unsigned char maxSnwPercentage,
                       unsigned char *outMask, size_t bandDimX, size_t bandDimY,
                       size_t sampleSize, float minNonzeroPercentage) {

  // join detfoo masks
  cudaPitchedPtr d_detfooMasksPtr;
  cudaExtent masksExtent =
      make_cudaExtent(bandDimX * sizeof(unsigned char), bandDimY, nDetfooMasks);
  gpuErrchk(cudaMalloc3D(&d_detfooMasksPtr, masksExtent));

  cudaMemcpy3DParms masksCopyParams = {};
  masksCopyParams.kind = cudaMemcpyKind::cudaMemcpyHostToDevice;
  masksCopyParams.extent = masksExtent;
  masksCopyParams.dstPtr = d_detfooMasksPtr;
  masksCopyParams.srcPtr =
      make_cudaPitchedPtr((void *)detfooMasks, sizeof(unsigned char) * bandDimX,
                          bandDimX, bandDimY);
  gpuErrchk(cudaMemcpy3D(&masksCopyParams));

  cudaPitchedPtr d_outPtr;
  d_outPtr.xsize = sizeof(unsigned char) * bandDimX;
  d_outPtr.ysize = bandDimY;
  gpuErrchk(cudaMallocPitch(&d_outPtr.ptr, &d_outPtr.pitch, d_outPtr.xsize,
                            d_outPtr.ysize));
  gpuErrchk(cudaMemset2D(d_outPtr.ptr, d_outPtr.pitch, 1,
                         bandDimX * sizeof(unsigned char), bandDimY));

  const int threadDim = 16;
  dim3 threadsPerBlock(threadDim, threadDim);
  dim3 blocksPerGrid(bandDimX / threadDim, bandDimY / threadDim);

  if (bandDimX % threadDim) {
    blocksPerGrid.x++;
  }

  if (bandDimY % threadDim) {
    blocksPerGrid.y++;
  }

  JoinUCharMasks<<<blocksPerGrid, threadsPerBlock>>>(
      d_detfooMasksPtr, d_outPtr, bandDimX, bandDimY, nDetfooMasks);
  cudaStreamSynchronize(0);
  gpuErrchk(cudaPeekAtLastError());

  gpuErrchkPassthrough(cudaFree(d_detfooMasksPtr.ptr));

  // join cld mask
  cudaPitchedPtr d_cldMaskPtr = {};
  d_cldMaskPtr.xsize = bandDimX;
  d_cldMaskPtr.ysize = bandDimY;
  gpuErrchk(cudaMallocPitch(&d_cldMaskPtr.ptr, &d_cldMaskPtr.pitch,
                            d_cldMaskPtr.xsize * sizeof(unsigned char),
                            d_cldMaskPtr.ysize));
  gpuErrchk(cudaMemcpy2D(d_cldMaskPtr.ptr, d_cldMaskPtr.pitch, (void *)cldMask,
                         bandDimX * sizeof(unsigned char),
                         bandDimX * sizeof(unsigned char), bandDimY,
                         cudaMemcpyHostToDevice));
  JoinUCharMasks<<<blocksPerGrid, threadsPerBlock>>>(
      d_cldMaskPtr, d_outPtr, bandDimX, bandDimY, 1, 0, maxCldPercentage);
  cudaStreamSynchronize(0);
  gpuErrchk(cudaPeekAtLastError());
  // join snw mask
  cudaPitchedPtr d_snwMaskPtr = {};
  d_snwMaskPtr.xsize = bandDimX;
  d_snwMaskPtr.ysize = bandDimY;
  gpuErrchk(cudaMallocPitch(&d_snwMaskPtr.ptr, &d_snwMaskPtr.pitch,
                            d_snwMaskPtr.xsize * sizeof(unsigned char),
                            d_snwMaskPtr.ysize));
  gpuErrchk(cudaMemcpy2D(d_snwMaskPtr.ptr, d_snwMaskPtr.pitch, (void *)snwMask,
                         bandDimX * sizeof(unsigned char),
                         bandDimX * sizeof(unsigned char), bandDimY,
                         cudaMemcpyHostToDevice));
  JoinUCharMasks<<<blocksPerGrid, threadsPerBlock>>>(
      d_snwMaskPtr, d_outPtr, bandDimX, bandDimY, 1, 0, maxSnwPercentage);
  cudaStreamSynchronize(0);
  gpuErrchk(cudaPeekAtLastError());
  gpuErrchkPassthrough(cudaFree(d_snwMaskPtr.ptr));

  // std::cout << "finished b" << std::endl;

  // const int threadDim = 16;
  // dim3 threadsPerBlock(threadDim, threadDim);
  // dim3 blocksPerGrid(bandDimX / threadDim + (threadDim - bandDimX %
  // threadDim),
  //                    bandDimY / threadDim + (threadDim - bandDimY %
  //                    threadDim));
  int *d_rowSums;
  size_t d_rowSums_pitch;
  gpuErrchk(cudaMallocPitch(&d_rowSums, &d_rowSums_pitch,
                            bandDimX * (sizeof(int)), bandDimY));

  BuildRowNonzeroSums<<<blocksPerGrid, threadsPerBlock>>>(
      d_outPtr, d_rowSums, d_rowSums_pitch, bandDimX, bandDimY, 1, sampleSize);
  cudaStreamSynchronize(0);
  gpuErrchk(cudaPeekAtLastError());

  // std::cout << "finished a" << std::endl;

  BuildSampleMap<<<blocksPerGrid, threadsPerBlock>>>(
      d_rowSums, d_rowSums_pitch, (unsigned char *)d_outPtr.ptr, d_outPtr.pitch,
      bandDimX, bandDimY, sampleSize, minNonzeroPercentage);
  gpuErrchk(cudaPeekAtLastError());

  cudaStreamSynchronize(0);
  gpuErrchk(cudaPeekAtLastError());
  // std::cout << "finished" << std::endl;

  // copy result to host
  gpuErrchk(cudaMemcpy2D((void *)outMask, sizeof(unsigned char) * bandDimX,
                         d_outPtr.ptr, d_outPtr.pitch,
                         bandDimX * sizeof(unsigned char), bandDimY,
                         cudaMemcpyKind::cudaMemcpyDeviceToHost));

  gpuErrchkPassthrough(cudaFree(d_rowSums));
  gpuErrchkPassthrough(cudaFree(d_outPtr.ptr));
}
} // namespace sats::cudaproc
