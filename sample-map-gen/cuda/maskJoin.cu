#include <cstdio>
#include <cuda_runtime_api.h>
#include <driver_functions.h>
#include <driver_types.h>
#include <optional>

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
      exit(code);
  }
}

/**
 * Returns a resulting uchar mask
 * Bounds are inclusive. Not specifying a bound assumes valid range is (0, 255]
 */
__global__ void JoinUCharMasks(cudaPitchedPtr masks, cudaPitchedPtr outMask,
                               size_t bandDimX, size_t bandDimY, size_t nMasks,
                               unsigned char boundMin = 1,
                               unsigned char boundMax = 255) {
  int r = blockDim.x * blockIdx.x + threadIdx.x;
  int c = blockDim.y * blockIdx.y + threadIdx.y;

  if (c > bandDimX || r > bandDimY) {
    return;
  }

  size_t maskPitch = masks.pitch * bandDimY;
  size_t maskRowPitch = masks.pitch;

  size_t outRowPitch = outMask.pitch;

  for (size_t maskNum = 0; maskNum < nMasks; maskNum++) {
    const unsigned char *mask =
        (unsigned char *)masks.ptr + maskNum * maskPitch;

    const unsigned char *maskRow = mask + maskRowPitch * r;
    unsigned char *outRow = (unsigned char *)outMask.ptr + outRowPitch * r;

    char maskValue = maskRow[c];

    if (maskValue < boundMin || maskValue > boundMax) {
      outRow[c] = 0;
    }
  }
}

void joinDetfooMasks(unsigned char *masks, unsigned char *outMask,
                     size_t bandDimX, size_t bandDimY, size_t nMasks) {
  cudaPitchedPtr d_masksPtr;
  cudaExtent masksExtent =
      make_cudaExtent(bandDimX * sizeof(unsigned char), bandDimY, nMasks);
  gpuErrchk(cudaMalloc3D(&d_masksPtr, masksExtent));

  cudaMemcpy3DParms masksCopyParams = {};
  masksCopyParams.kind = cudaMemcpyKind::cudaMemcpyHostToDevice;
  masksCopyParams.extent = masksExtent;
  masksCopyParams.dstPtr = d_masksPtr;
  masksCopyParams.srcPtr = make_cudaPitchedPtr(
      (void *)masks, sizeof(unsigned char) * bandDimX, bandDimX, bandDimY);
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
      d_masksPtr, d_outPtr, bandDimX, bandDimY, nMasks);
  gpuErrchk(cudaPeekAtLastError());

  gpuErrchkPassthrough(cudaFree(d_masksPtr.ptr));

  gpuErrchk(cudaMemcpy2D((void *)outMask, sizeof(unsigned char) * bandDimX,
                         d_outPtr.ptr, d_outPtr.pitch,
                         bandDimX * sizeof(unsigned char), bandDimY,
                         cudaMemcpyKind::cudaMemcpyDeviceToHost));
  gpuErrchkPassthrough(cudaFree(d_outPtr.ptr));
}

void joinMasks(unsigned char *detfooMasks, size_t nDetfooMasks,
               unsigned char *cldMask, unsigned char maxCldPercentage,
               unsigned char *snwMask, unsigned char maxSnwPercentage,
               unsigned char *outMask, size_t bandDimX, size_t bandDimY) {

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
  gpuErrchk(cudaPeekAtLastError());
  gpuErrchkPassthrough(cudaFree(d_cldMaskPtr.ptr));

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
  gpuErrchk(cudaPeekAtLastError());
  gpuErrchkPassthrough(cudaFree(d_snwMaskPtr.ptr));

  // copy result to host
  gpuErrchk(cudaMemcpy2D((void *)outMask, sizeof(unsigned char) * bandDimX,
                         d_outPtr.ptr, d_outPtr.pitch,
                         bandDimX * sizeof(unsigned char), bandDimY,
                         cudaMemcpyKind::cudaMemcpyDeviceToHost));
  gpuErrchkPassthrough(cudaFree(d_outPtr.ptr));
}
