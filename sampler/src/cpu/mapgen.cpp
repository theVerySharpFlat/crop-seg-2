#include "mapgen.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace sats::cpuproc {

void joinUCharMasks(uint8_t *masks, uint8_t *outMask, size_t bandDimX,
                    size_t bandDimY, size_t nMasks, unsigned char boundMin = 1,
                    unsigned char boundMax = 255) {
  for (int maskIdx = 0; maskIdx < nMasks; maskIdx++) {
    uint8_t *mask = masks + bandDimX * bandDimY * maskIdx;

    for (int x = 0; x < bandDimX; x++) {
      for (int y = 0; y < bandDimY; y++) {
        uint8_t maskVal = mask[y * bandDimX + x];
        if (maskVal < boundMin || maskVal > boundMax) {
          outMask[y * bandDimX + x] = 0;
        }
      }
    }
  }
}

void mapgen(uint8_t *mask, size_t bandDimX, size_t bandDimY, size_t sampleSize,
            float minNonzeroPercentage) {

  int *rowSums = (int *)malloc(sizeof(int) * bandDimY * bandDimX);

  for (size_t r = 0; r < bandDimY; r++) {
    for (size_t c = 0; c < bandDimX; c++) {

      if (c >= bandDimX - sampleSize) {
        rowSums[r * bandDimX + c] = 0;
        continue;
      }

      rowSums[r * bandDimX + c] = 0;

      if (c == 0) {
        for (int i = 0; i < sampleSize; i++) {
          if (mask[r * bandDimX + c + i]) {
            rowSums[r * bandDimX + c]++;
          }
        }
      } else {
        rowSums[r * bandDimX + c] = rowSums[r * bandDimX + c - 1] -
                                    mask[r * bandDimX + c - 1] +
                                    mask[r * bandDimX + c + sampleSize - 1];
      }
    }

    // std::cout << "row: " << r << std::endl;
  }

  size_t nOK = 0;
  for (size_t c = 0; c < bandDimX; c++) {
    int prevTotal = 0;
    for (size_t r = 0; r < bandDimY; r++) {

      if (r >= bandDimY - sampleSize || c >= bandDimX - sampleSize) {
        mask[r * bandDimX + c] = 0;
        // rowSums[r * bandDimX + c] = 0;
        continue;
      }

      // rowSums[r * bandDimX + c] = 0;

      int total = 0;
      if (r == 0) {
        for (int i = 0; i < sampleSize; i++) {
          total += rowSums[(r + i) * bandDimX + c];
        }
      } else {
        total = prevTotal - rowSums[(r - 1) * bandDimX + c] +
                rowSums[(r + sampleSize - 1) * bandDimX + c];
      }

      prevTotal = total;

      mask[r * bandDimX + c] =
          (((float)total / sampleSize / sampleSize) > minNonzeroPercentage);
      nOK += mask[r * bandDimX + c];
    }

    // std::cout << "row: " << r << std::endl;
  }
  free(rowSums);
}

void generateSampleMap(unsigned char *detfooMasks, size_t nDetfooMasks,
                       unsigned char *cldMask, unsigned char maxCldPercentage,
                       unsigned char *snwMask, unsigned char maxSnwPercentage,
                       unsigned char *sclMask, unsigned char *outMask,
                       size_t bandDimX, size_t bandDimY, size_t sampleSize,
                       float minNonzeroPercentage) {

  std::memset(outMask, 1, bandDimX * bandDimY * sizeof(uint8_t));

  joinUCharMasks(detfooMasks, outMask, bandDimX, bandDimY, nDetfooMasks);
  joinUCharMasks(cldMask, outMask, bandDimX, bandDimY, 1, 0, maxCldPercentage);
  joinUCharMasks(snwMask, outMask, bandDimX, bandDimY, 1, 0, maxSnwPercentage);
  joinUCharMasks(sclMask, outMask, bandDimX, bandDimY, 1, 4, 6);

  mapgen(outMask, bandDimX, bandDimY, sampleSize, minNonzeroPercentage);
}

} // namespace sats::cpuproc
