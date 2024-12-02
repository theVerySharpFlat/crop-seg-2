#include <algorithm>
#include <cstdio>
#include <iostream>

#define gpuErrchk(ans)                                                         \
  {                                                                            \
    gpuAssert((ans), __FILE__, __LINE__);                                      \
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

__global__ void BuildRowNonzeroSums(cudaPitchedPtr bands, int *out,
                                    size_t out_pitch, int bandDimX,
                                    int bandDimY, int nBands, int sampleSize) {
  int r = blockDim.x * blockIdx.x + threadIdx.x;
  int c = blockDim.y * blockIdx.y + threadIdx.y;

  if (c > bandDimX - sampleSize || r > bandDimY - sampleSize) {
    return;
  }

  int pitch = bands.pitch;
  int slicePitch = bands.pitch * bandDimY;

  int *outRow = (int *)((char *)out + pitch * r);
  outRow[c] = 0;

  for (int band = 0; band < nBands; band++) {
    char *bandSlice = (char *)bands.ptr + band * slicePitch;
    float *row = (float *)(bandSlice + r * pitch);

    for (int k = c; k < c + sampleSize; c++) {
      if (row[k] != 0.0f) {
        outRow[c] = 0;
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

__global__ void BuildSampleMap(int *rowSums, size_t rowSums_pitch, int *out,
                               size_t out_pitch, int bandDimX, int bandDimY,
                               int sampleSize) {
  int r = blockDim.x * blockIdx.x + threadIdx.x;
  int c = blockDim.y * blockIdx.y + threadIdx.y;

  if (c > bandDimX - sampleSize || r > bandDimY - sampleSize) {
    return;
  }

  int *outRow = (int *)((char *)out + out_pitch * r);
  outRow[c] = 0;

  for (int k = r; k < r + sampleSize; r++) {
    int *rowSumsRow = (int *)((char *)rowSums + rowSums_pitch * k);

    outRow[c] += rowSumsRow[c];
  }
}

void buildSampleMap(float ***bands, int **out, int bandDimX, int bandDimY,
                    int nBands, int sampleSize) {
  cudaError setErr = cudaSetDevice(0);

  if (setErr == cudaErrorInvalidDevice) {
    std::cout << "invalid device!" << std::endl;
  } else if (setErr == cudaErrorDevicesUnavailable) {
    std::cout << "devices unavailable!" << std::endl;
  }

  cudaPitchedPtr d_bandsPtr;
  cudaExtent extent =
      (make_cudaExtent(bandDimX * sizeof(float), bandDimY, nBands));
  std::cout << "allocating: " << extent.width * extent.depth * extent.height
            << std::endl;

  size_t total, free, temp, used;
  setErr = cudaMemGetInfo(&temp, &total); // get memory information

  std::cout << "setErr: " << setErr << " " << cudaGetErrorName(setErr)
            << std::endl;

  printf("Total mem: %lu \t free mem before malloc: %lu\n", total,
         temp); // output

  gpuErrchk(cudaMalloc3D(&d_bandsPtr, extent));

  cudaMemcpy3DParms copyParams = {};
  copyParams.extent = extent;
  copyParams.kind = cudaMemcpyKind::cudaMemcpyHostToDevice;
  copyParams.dstPtr = d_bandsPtr;
  copyParams.srcPtr =
      make_cudaPitchedPtr(bands, sizeof(float) * bandDimX, bandDimX, bandDimY);
  gpuErrchk(cudaMemcpy3D(&copyParams));

  int *d_rowSums;
  size_t d_rowSums_pitch;
  gpuErrchk(cudaMallocPitch(&d_rowSums, &d_rowSums_pitch,
                            bandDimX * (sizeof(int)), bandDimY));

  const int threadDim = 16;
  dim3 threadsPerBlock(threadDim, threadDim);
  dim3 blocksPerGrid(bandDimX / threadDim + (threadDim - bandDimX % threadDim),
                     bandDimY / threadDim + (threadDim - bandDimY % threadDim));
  BuildRowNonzeroSums<<<blocksPerGrid, threadsPerBlock>>>(
      d_bandsPtr, d_rowSums, d_rowSums_pitch, bandDimX, bandDimY, nBands,
      sampleSize);
  gpuErrchk(cudaPeekAtLastError());

  int *d_out;
  size_t d_out_pitch;
  gpuErrchk(cudaMallocPitch(&d_out, &d_out_pitch, bandDimX * (sizeof(int)),
                            bandDimY));
  BuildSampleMap<<<blocksPerGrid, threadsPerBlock>>>(
      d_rowSums, d_rowSums_pitch, d_out, d_out_pitch, bandDimX, bandDimY,
      sampleSize);
  gpuErrchk(cudaPeekAtLastError());
}
