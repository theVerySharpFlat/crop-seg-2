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

  std::vector<float *> randomSample();

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
    std::string productName;
    std::string tileName;

    size_t maxDimX, maxDimY;

    std::optional<SampleCache> cache;
  };

  std::vector<SampleInfo> infos;

  size_t computeSampleIndex(size_t okIndex, const SampleCache &cache);

  std::pair<std::optional<SampleCache>, std::string>
  genCache(const SampleInfo &info, SampleCacheGenOptions genOptions);

  SampleCacheGenOptions cacheGenOptions;

#ifdef TESTING
  FRIEND_TEST(SamplerTest, IndexTest);
#endif
};

} // namespace sats
