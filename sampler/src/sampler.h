#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace sats {

struct DateRange {
  size_t minYear;
  size_t minMonth;
  size_t minDay;

  size_t maxYear;
  size_t maxMonth;
  size_t maxDay;
};

class Sampler {
public:
  struct SampleCacheGenOptions {
    float minOKPercentage;
    size_t sampleDim;

    uint8_t cldMax, snwMax;
  };

  Sampler() = delete;

  Sampler(const std::filesystem::path &path, SampleCacheGenOptions options,
          std::optional<DateRange> dateRange, bool preproc = true);

private:
  struct SampleCache {
    uint8_t *bitrange;
    size_t size;

    size_t nOK;

    size_t nRows;
    size_t nCols;
  };

  struct SampleInfo {
    std::filesystem::path path;

    size_t year, month, day;
    std::string tileName;

    std::optional<SampleCache> cache;
  };

  std::vector<SampleInfo> infos;

  std::pair<std::optional<SampleCache>, std::string>
  genCache(const std::filesystem::path &path, SampleCacheGenOptions genOptions);

  SampleCacheGenOptions cacheGenOptions;
};

} // namespace sats
