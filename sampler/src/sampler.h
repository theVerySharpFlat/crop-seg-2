#pragma once

#include <cstdint>
#include <filesystem>
#include <omp.h>
#include <optional>
#include <vector>

#include <sqlite3.h>

class SamplerTest_IndexTest_Test;
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
  struct SampleOptions {
    std::filesystem::path dbPath;
    size_t nCacheGenThreads;
    size_t nCacheQueryThreads;
  };

  struct SampleCacheGenOptions {
    float minOKPercentage;
    size_t sampleDim;

    uint8_t cldMax, snwMax;
  };

  Sampler() = delete;

  virtual ~Sampler();

  Sampler(const std::filesystem::path &path, SampleOptions sampleOptions,
          SampleCacheGenOptions options, std::optional<DateRange> dateRange,
          bool preproc = true);

  std::vector<float *> randomSample();

  std::vector<std::vector<float *>> randomSampleV2(size_t n);

private:
  friend class ::SamplerTest_IndexTest_Test;
  // FRIEND_TEST(SamplerTest, IndexTest);

  struct SampleCache {
    uint8_t *bitrange = nullptr;
    size_t size;

    size_t nOK;

    size_t nRows;
    size_t nCols;
  };

  inline void freeSampleCache(SampleCache &cache) {
    if (cache.bitrange) {
      free(cache.bitrange);
      cache.bitrange = nullptr;
    }
  }

  struct ComputationCache {
    SampleCache sampleCache;

    size_t maxDimX, maxDimY;

    // For cache invalidation
    std::string productName;
    size_t unixModTime;
  };

  struct SampleInfo {
    std::filesystem::path path;

    size_t year, month, day;
    std::string productName;
    std::string tileName;

    size_t maxDimX, maxDimY;

    // std::optional<SampleCache> cache;
  };

  std::vector<SampleInfo> infos;

  size_t computeSampleIndex(size_t okIndex, const SampleCache &cache);

  // omp_lock_t sqlWriteLock;
  std::vector<sqlite3 *> connectionPool;
  std::optional<std::string> setupSQLCache();

  std::optional<std::string> getCacheEntry(sqlite3 *conn,
                                           const SampleInfo &info,
                                           ComputationCache *cache,
                                           bool *oCachePresent);
  std::optional<std::string> writeCacheEntry(sqlite3 *conn,
                                             const ComputationCache &cache);

  bool cacheValid(const SampleInfo &info, const ComputationCache &cache);

  std::optional<std::string> ensureCache();

  std::pair<std::optional<SampleCache>, std::string>
  genSampleCache(const SampleInfo &info, SampleCacheGenOptions genOptions);

  SampleCacheGenOptions cacheGenOptions;
  SampleOptions sampleOptions;
};

} // namespace sats
