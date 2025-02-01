#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sats::cpuproc {
std::vector<float> percentiles(const float *data, size_t len,
                               const std::vector<size_t> percentiles);
} // namespace sats::cpuproc
