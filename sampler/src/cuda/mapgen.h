#pragma once

void buildSampleMap(float ***bands, int **out, int bandDimX, int bandDimY,
                    int nBands, int sampleSize);

void generateSampleMap(unsigned char *detfooMasks, size_t nDetfooMasks,
                       unsigned char *cldMask, unsigned char maxCldPercentage,
                       unsigned char *snwMask, unsigned char maxSnwPercentage,
                       unsigned char *outMask, size_t bandDimX, size_t bandDimY,
                       size_t sampleSize, float minNonzeroPercentage);
