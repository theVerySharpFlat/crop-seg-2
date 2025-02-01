#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sats::cuda {
std::vector<float> percentiles(const float *data, size_t len,
                               const std::vector<size_t> percentiles);
} // namespace sats::cuda
