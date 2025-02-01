#include "percentile.h"

#include <iostream>
#include <thrust/device_ptr.h>
#include <thrust/device_vector.h>
#include <thrust/execution_policy.h>
#include <thrust/sort.h>

std::vector<float>
sats::cuda::percentiles(const float *data, size_t len,
                        const std::vector<size_t> percentiles) {
  std::vector<float> sorted(len);

  auto dev = thrust::device_vector<float>(len);
  thrust::copy(data, data + len, dev.begin());

  thrust::sort(thrust::device, dev.begin(), dev.end());

  thrust::copy(dev.begin(), dev.end(), sorted.begin());

  std::vector<float> outPercentiles;
  outPercentiles.reserve(percentiles.size());

  for (const auto &percentile : percentiles) {
    assert(percentile >= 0);
    assert(percentile <= 100);

    size_t index = ((float)percentile / 100.0) * len;
    std::cout << "index: " << index << std::endl;

    outPercentiles.push_back(sorted[index]);
  }

  return outPercentiles;
}
