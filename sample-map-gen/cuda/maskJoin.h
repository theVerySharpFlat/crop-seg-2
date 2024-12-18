#pragma once

#include <cstddef>

// void joinDetfooMasks(unsigned char *masks, unsigned char *outMask,
//                      size_t bandDimX, size_t bandDimY, size_t nMasks);
void joinMasks(unsigned char *detfooMasks, size_t nDetfooMasks,
               unsigned char *cldMask, unsigned char maxCldPercentage,
               unsigned char *snwMask, unsigned char maxSnwPercentage,
               unsigned char *outMask, size_t bandDimX, size_t bandDimY);
