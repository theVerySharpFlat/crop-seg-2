#include "percentile.h"
#include <iostream>

std::vector<float>
sats::cpuproc::percentiles(const float *data, size_t len,
                           const std::vector<size_t> percentiles) {
  std::vector<float> sorted(len);
  memcpy(&sorted[0], data, len);

  std::sort(sorted.begin(), sorted.end());

  std::vector<float> outPercentiles;
  outPercentiles.reserve(len);

  for (const auto &percentile : percentiles) {
    assert(percentile >= 0);
    assert(percentile <= 100);

    size_t index = ((float)percentile / 100.0) * len;
    std::cout << "index: " << index << std::endl;

    outPercentiles.push_back(sorted[index]);
  }

  return outPercentiles;
}
