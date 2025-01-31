#include "sampler.h"
#include "cpu/mapgen.h"
#include "cuda/mapgen.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <optional>
#include <regex>
#include <sqlite3.h>
#include <unistd.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <iostream>
#include <omp.h>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sats {

static const std::string flavors[] = {"HIRES", "LOWRES", "MSK"};

static std::optional<std::string>
getProductName(const std::filesystem::path &path) {
  std::regex subRe("S2[A-Z](.*)\\.SAFE");
  std::smatch subMatch;

  std::string fname = path.filename().string();
  if (!std::regex_search(fname, subMatch, subRe)) {
    return std::nullopt;
  }

  return subMatch.str();
}

static std::optional<std::string>
getSubProduct(const std::filesystem::path &path, const std::string &flavor) {
  const auto &productName = getProductName(path);

  if (!productName) {
    return std::nullopt;
  }

  std::string dsPath =
      "/vsizip/" + (std::filesystem::canonical(path) /
                    (productName.value() + "-" + flavor + ".tif"))
                       .string();

  return dsPath;
}

static bool getProductDims(const std::filesystem::path &path,
                           const std::string &flavor, size_t *xDim,
                           size_t *yDim) {
  const auto &productName = getProductName(path);

  if (!productName) {
    std::cout << "could not get product dims: could not get product name"
              << std::endl;
    return false;
  }

  const auto productPath = getSubProduct(path, flavor);

  if (!productPath) {
    std::cout << "failed to get product dims: could not determine product path"
              << std::endl;
  }

  GDALDataset *ds = GDALDataset::FromHandle(
      GDALOpen(productPath.value().c_str(), GDALAccess::GA_ReadOnly));

  if (!ds) {
    std::cout << "could not open ds to get dims" << std::endl;

    return false;
  }

  *xDim = ds->GetRasterXSize();
  *yDim = ds->GetRasterYSize();

  ds->Close();

  return true;
}

static std::optional<std::pair<size_t, size_t>>
getMaxResolution(const std::filesystem::path &path) {
  size_t maxDimX = 0, maxDimY = 0;
  bool cont = false;
  for (const auto &flavor : flavors) {
    size_t dimX, dimY;
    if (!getProductDims(path, flavor, &dimX, &dimY)) {
      // std::cout << "failed to get product dimensions for " << path
      //           << " flavor: " << flavor << std::endl;
      return std::nullopt;
    }

    if (dimX == 0 || dimY == 0) {
      // std::cout << "dimX or dimY is zero!" << std::endl;
      return std::nullopt;
    }

    if (maxDimX != 0 && maxDimY != 0) {
      if (std::max(maxDimX, dimX) % std::min(maxDimX, dimX) != 0) {
        return std::nullopt;
      }
    }

    maxDimX = std::max(maxDimX, dimX);
    maxDimY = std::max(maxDimY, dimY);
  }

  if (cont) {
    return std::nullopt;
  }

  return std::make_pair(maxDimX, maxDimY);
}

std::optional<std::string>
Sampler::writeCacheEntry(sqlite3 *conn, const ComputationCache &cache) {
  const char *updateQuery =
      "INSERT OR REPLACE INTO COMPUTATIONS(PRODUCT, SAMPLEMAP, SAMPLEDIMX, "
      "SAMPLEDIMY, NOK, MAXDIMX, MAXDIMY, LASTMOD)"
      "  VALUES(?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(conn, updateQuery, -1, &stmt, NULL) != SQLITE_OK) {
    return "failed to prepare statment: " + std::string(sqlite3_errmsg(conn));
  }

  // std::cout << "lastmod: " << cache.unixModTime << std::endl;

  sqlite3_bind_text(stmt, 1, cache.productName.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 2, cache.sampleCache.bitrange,
                    (int)cache.sampleCache.size, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, *((int64_t *)&cache.sampleCache.nCols));
  sqlite3_bind_int64(stmt, 4, *((int64_t *)&cache.sampleCache.nRows));
  sqlite3_bind_int64(stmt, 5, *((int64_t *)&cache.sampleCache.nOK));
  sqlite3_bind_int64(stmt, 6, *((int64_t *)&cache.maxDimX));
  sqlite3_bind_int64(stmt, 7, *((int64_t *)&cache.maxDimY));
  sqlite3_bind_int64(stmt, 8, *((int64_t *)&cache.unixModTime));

  int ret;
  while (ret = sqlite3_step(stmt), ret == SQLITE_ROW || ret == SQLITE_OK) {
  }

  if (ret != SQLITE_DONE) {
    auto errmsg = "sqlite step error: " + std::string(sqlite3_errmsg(conn));
    sqlite3_finalize(stmt);
    return errmsg;
  }

  sqlite3_finalize(stmt);

  return std::nullopt;
}

std::optional<std::string> Sampler::setupSQLCache() {

  size_t nConns = std::max(sampleOptions.nCacheGenThreads,
                           sampleOptions.nCacheQueryThreads);
  connectionPool.resize(nConns);
  for (size_t i = 0; i < nConns; i++) {
    int ret = sqlite3_open_v2(
        sampleOptions.dbPath.c_str(), &connectionPool[i],
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);

    if (ret != SQLITE_OK) {
      return std::format("sqlite_open_v2: {}",
                         sqlite3_errmsg(connectionPool[i]));
    }
  }

  const char *tableSetup =
      "CREATE TABLE IF NOT EXISTS COMPUTATIONS("
      "PRODUCT       TEXT                PRIMARY KEY     NOT NULL,"
      "SAMPLEMAP     BLOB                                NOT NULL,"
      "SAMPLEDIMX    UNSIGNED BIG INT                    NOT NULL,"
      "SAMPLEDIMY    UNSIGNED BIG INT                    NOT NULL,"
      "NOK           UNSIGNED BIG INT                    NOT NULL,"
      ""
      "MAXDIMX       UNSIGNED BIG INT                    NOT NULL,"
      "MAXDIMY       UNSIGNED BIG INT                    NOT NULL,"
      ""
      "LASTMOD       UNSIGNED BIG INT                    NOT NULL"
      ");";

  // assert(sampleOptions.nThreads > 0);

  sqlite3_stmt *stmt;
  int ret = sqlite3_prepare_v2(connectionPool[0], tableSetup, -1, &stmt, NULL);
  if (ret != SQLITE_OK) {
    return std::format("sqlite3_prepare_v2: {}",
                       sqlite3_errmsg(connectionPool[0]));
  }

  while (ret = sqlite3_step(stmt), ret == SQLITE_ROW && ret == SQLITE_OK)
    ;

  if (ret != SQLITE_DONE) {
    auto errmsg =
        std::format("sqlite3_step_v2: {}", sqlite3_errmsg(connectionPool[0]));

    sqlite3_finalize(stmt);

    return errmsg;
  }

  sqlite3_finalize(stmt);

  return std::nullopt;
}

std::optional<std::string> Sampler::getCacheEntry(sqlite3 *conn,
                                                  const SampleInfo &info,
                                                  ComputationCache *cache,
                                                  bool *oCachePresent) {
  const char *query = "SELECT SAMPLEMAP, SAMPLEDIMX, "
                      "SAMPLEDIMY, NOK, MAXDIMX, MAXDIMY, LASTMOD "
                      "FROM COMPUTATIONS WHERE PRODUCT = ?;";

  sqlite3_stmt *stmt;
  int ret = sqlite3_prepare_v2(conn, query, -1, &stmt, NULL);
  if (ret != SQLITE_OK) {
    return std::format("sqlite3_prepare_v2: {}", sqlite3_errmsg(conn));
  }

  sqlite3_bind_text(stmt, 1, info.productName.c_str(), -1, SQLITE_STATIC);

  ret = sqlite3_step(stmt);

  if (ret != SQLITE_ROW) {
    *oCachePresent = false;
    if (ret == SQLITE_DONE) {
      sqlite3_finalize(stmt);
      return std::nullopt;
    }

    auto errmsg = sqlite3_errmsg(conn);
    sqlite3_finalize(stmt);
    return std::string(errmsg);
  }

  size_t samplemapSize = sqlite3_column_bytes(stmt, 0);
  const void *sqliteSampleMap = sqlite3_column_blob(stmt, 0);

  uint8_t *sampleMap = (uint8_t *)malloc(samplemapSize);
  memcpy(sampleMap, sqliteSampleMap, samplemapSize);

  uint64_t sampleDimX;
  int64_t sqliteSampleDimX = sqlite3_column_int64(stmt, 1);
  sampleDimX = *((uint64_t *)&sqliteSampleDimX); // evil

  uint64_t sampleDimY;
  int64_t sqliteSampleDimY = sqlite3_column_int64(stmt, 2);
  sampleDimY = *((uint64_t *)&sqliteSampleDimY); // evil

  uint64_t nOK;
  int64_t sqliteNOK = sqlite3_column_int64(stmt, 3);
  nOK = *((uint64_t *)&sqliteNOK); // evil

  uint64_t maxDimX;
  int64_t sqliteMaxDimX = sqlite3_column_int64(stmt, 4);
  maxDimX = *((uint64_t *)&sqliteMaxDimX); // evil

  uint64_t maxDimY;
  int64_t sqliteMaxDimY = sqlite3_column_int64(stmt, 5);
  maxDimY = *((uint64_t *)&sqliteMaxDimY); // evil

  uint64_t lastMod;
  int64_t sqliteLastmod = sqlite3_column_int64(stmt, 6);
  lastMod = *((uint64_t *)&sqliteLastmod); // evil

  sqlite3_finalize(stmt);

  // std::cout << "maxDimY from read: " << maxDimY << std::endl;
  // std::cout << "lastmod from read: " << lastMod << std::endl;

  cache->maxDimX = maxDimX;
  cache->maxDimY = maxDimY;
  cache->unixModTime = lastMod;

  cache->sampleCache.nOK = nOK;
  cache->sampleCache.bitrange = sampleMap;
  cache->sampleCache.size = samplemapSize;
  cache->sampleCache.nCols = sampleDimX;
  cache->sampleCache.nRows = sampleDimY;

  cache->productName = info.productName;

  *oCachePresent = true;

  return std::nullopt;
}

bool Sampler::cacheValid(const SampleInfo &info,
                         const ComputationCache &cache) {
  assert(std::filesystem::exists(info.path));

  if (cache.productName != info.productName) {
    // std::cout << std::format("cache differs in name {},  {}",
    // cache.productName,
    //                          info.productName)
    //           << std::endl;
    return false;
  }

  struct stat s;
  stat(info.path.c_str(), &s);

  if ((uint64_t)s.st_mtim.tv_sec != cache.unixModTime) {
    // std::cout << std::format("cache differs in time {}, {}",
    //                          (uint64_t)s.st_mtim.tv_sec, cache.unixModTime)
    //           << std::endl;
    return false;
  }

  return true;
}

Sampler::Sampler(const std::filesystem::path &dataDir,
                 SampleOptions sampleOptions,
                 SampleCacheGenOptions cacheGenOptions,
                 std::optional<DateRange> dateRange, bool preproc)
    : cacheGenOptions(cacheGenOptions), sampleOptions(sampleOptions) {
  GDALAllRegister();

  std::regex re("^REPACK_S2[A-Z]_[A-Z0-9]+_(\\d{4})(\\d{2})(\\d{2})T[1-9]+_[A-"
                "Z0-9]+_[A-Z0-9]+_([A-Z0-9]+)_[A-Z0-9]+\\.SAFE\\.zip$");

  for (const auto &path :
       std::filesystem::recursive_directory_iterator(dataDir)) {
    if (path.is_directory())
      continue;

    std::smatch match;

    const std::string &fname = path.path().filename().string();
    if (!std::regex_match(fname, match, re))
      continue;

    size_t year = std::stoi(match[1]);
    size_t month = std::stoi(match[2]);
    size_t day = std::stoi(match[3]);

    std::string tileName = match[4];

    if (dateRange) {
      size_t dateRangeMin = dateRange->minYear * 12 * 31 +
                            dateRange->minMonth * 31 + dateRange->minDay;

      size_t dateRangeMax = dateRange->maxYear * 12 * 31 +
                            dateRange->maxMonth * 31 + dateRange->maxDay;

      size_t date = year * 12 * 31 + month * 31 + day;

      if (date < dateRangeMin || date > dateRangeMax)
        continue;
    }

    const auto productName = getProductName(path);
    if (!productName) {
      std::cout << "could not get product name for " << path << std::endl;
      continue;
    }

    // Get highest resolution dataset and make sure resolutions are whole number
    // multiple of eachother
    const auto maxRes = getMaxResolution(path);
    if (!maxRes) {
      std::cout
          << "resolutions in dataset are malformed or are of incorrect scale"
          << std::endl;
      continue;
    }

    infos.push_back({
        .path = std::filesystem::canonical(path.path()),
        .year = year,
        .month = month,
        .day = day,
        .productName = productName.value(),
        .tileName = tileName,
        .maxDimX = maxRes->first,
        .maxDimY = maxRes->second,
        // .cache = std::nullopt,
    });

    std::cout << year << " " << month << " " << day << " " << tileName << ": "
              << path << std::endl;
  }

  auto sqlInitError = setupSQLCache();
  if (sqlInitError) {
    std::cout << "sql init error: " << sqlInitError.value() << std::endl;
  } else {
    std::cout << "no sql init errors?" << std::endl;
  }

  auto cacheError = ensureCache();

  if (cacheError) {
    std::cout << "cache error: " << cacheError.value() << std::endl;
  } else {
    std::cout << "no cache errors?" << std::endl;
  }
}

size_t Sampler::computeSampleIndex(size_t sampleOKIndex,
                                   const SampleCache &cache) {
  size_t sampleIndex = 0;
  size_t currentCount = 0;

  for (size_t i = 0; i < cache.size / 8; i++) {
    size_t cnt = std::popcount(((uint64_t *)cache.bitrange)[i]);

    if (currentCount + cnt > (sampleOKIndex + 1)) {
      cnt = 0;
      size_t j = 0;
      for (; j < 64 && cnt < ((sampleOKIndex + 1) - currentCount); j++) {
        cnt += (size_t)((bool)((1ul << (/* 63 -  */ j)) &
                               ((((uint64_t *)cache.bitrange)[i]))));
      }

      assert(cnt == (sampleOKIndex + 1 - currentCount));

      sampleIndex += j;
      currentCount += cnt;
      // break;
    } else if (currentCount + cnt == (sampleOKIndex + 1)) {
      currentCount += cnt;
      assert(((uint64_t *)cache.bitrange)[i] != 0);
      sampleIndex += 64 - __builtin_clzl(((uint64_t *)cache.bitrange)[i]);
    } else {
      currentCount += cnt;
      sampleIndex += 64;
    }

    if (currentCount == (sampleOKIndex + 1)) {
      break;
    }
  }

  return sampleIndex - 1;
}

std::pair<std::optional<Sampler::SampleCache>, std::string>
Sampler::genSampleCache(const SampleInfo &info,
                        Sampler::SampleCacheGenOptions genOptions) {
  const std::filesystem::path &path = info.path;

  const auto productName = getProductName(path);
  if (!productName) {
    return std::make_pair(std::nullopt, "Could not get product name from path");
  }

  std::filesystem::path mskProductPath =
      path / std::filesystem::path(productName.value() + "-MSK.tif");

  GDALDataset *ds = GDALDataset::Open(
      ("vrt:///vsizip/" + mskProductPath.string() +
       // std::format("?outsize={},{}", info.maxDimX, info.maxDimY))
       "")
          .c_str());

  if (!ds) {
    return std::make_pair(std::nullopt, "failed to open file " +
                                            std::string("/vsizip/") +
                                            mskProductPath.string());
  }

  size_t nBands = ds->GetBands().size();
  if (nBands < 2) {
    return std::make_pair(std::nullopt,
                          "mask dataset doesn't have enough bands!");
  }

  for (size_t bandNum = 0; bandNum < nBands; bandNum++) {
    if (ds->GetRasterBand(bandNum + 1)->GetRasterDataType() != GDT_Byte) {
      return std::make_pair(std::nullopt, "Band " + std::to_string(bandNum) +
                                              " datatype is not GDT_Byte");
    }
  }

  size_t cldIdx = nBands - 2;
  size_t snwIdx = nBands - 1;

  uint8_t ***bands = (uint8_t ***)malloc(
      sizeof(uint8_t) * ds->GetRasterXSize() * ds->GetRasterYSize() * nBands);
  for (size_t bandNum = 0; bandNum < nBands; bandNum++) {
    CPLErr err = ds->GetRasterBand(bandNum + 1)
                     ->RasterIO(GDALRWFlag::GF_Read, 0, 0, ds->GetRasterXSize(),
                                ds->GetRasterYSize(),
                                (char *)bands + bandNum * ds->GetRasterXSize() *
                                                    ds->GetRasterYSize(),
                                ds->GetRasterXSize(), ds->GetRasterYSize(),
                                GDT_Byte, 0, 0);

    if (err) {
      free(bands);
      return std::make_pair(std::nullopt,
                            "Band " + std::to_string(bandNum) +
                                "read failed: " + std::to_string(err));
    }
  }

  uint8_t *stage = (uint8_t *)malloc(sizeof(uint8_t) * ds->GetRasterXSize() *
                                     ds->GetRasterYSize());

  size_t nPixels = ds->GetRasterXSize() * ds->GetRasterYSize();

  size_t scalingFactor = info.maxDimX / ds->GetRasterXSize();

#if 0 && HAS_CUDA
  cudaproc::generateSampleMap(
      (unsigned char *)bands, nBands - 2,
      (unsigned char *)((char *)bands + cldIdx * nPixels),
      cacheGenOptions.cldMax,
      (unsigned char *)((char *)bands + snwIdx * nPixels),
      cacheGenOptions.snwMax, (unsigned char *)stage, ds->GetRasterXSize(),
      ds->GetRasterYSize(), cacheGenOptions.sampleDim / scalingFactor,
      cacheGenOptions.minOKPercentage);
#else
  cpuproc::generateSampleMap(
      (unsigned char *)bands, nBands - 2,
      (unsigned char *)((char *)bands + cldIdx * nPixels),
      cacheGenOptions.cldMax,
      (unsigned char *)((char *)bands + snwIdx * nPixels),
      cacheGenOptions.snwMax, (unsigned char *)stage, ds->GetRasterXSize(),
      ds->GetRasterYSize(), cacheGenOptions.sampleDim,
      cacheGenOptions.minOKPercentage);
#endif

  size_t nPixelsCondensed = (size_t)(std::ceil(nPixels / 64.0) * 8);

  uint8_t *condensed = (uint8_t *)malloc(nPixelsCondensed);
  // zero memory, could use calloc instead
  memset(condensed, 0, nPixelsCondensed);

  size_t okTotal = 0;
  for (size_t i = 0; i < nPixelsCondensed; i++) {
    condensed[i] = 0;

    // condensed[i] = (stage[i * 8]) != 0;

    condensed[i] = 0;
    for (int j = 0; j < 8 && ((i * 8 + j) < nPixels); j++) {
      uint8_t ok = (((uint8_t *)stage)[i * 8 + j]) != 0;

      if (((10980 / 2) - ((i * 8 + j) % (10980 / 2))) < 128) {
        assert(!ok);
      }

      assert(ok == 1 || ok == 0);
      assert(7 - j >= 0);
      assert(j <= 7);

      okTotal += ok;
      condensed[i] |= (ok << (j));
    }
  }

  free(stage);
  free(bands);
  ds->Close();

  auto ret = std::make_pair(SampleCache{}, "");
  // ret.first = std::move(cache);
  ret.first.bitrange = condensed;
  ret.first.size = nPixelsCondensed;
  ret.first.nOK = okTotal;
  ret.first.nRows = (size_t)ds->GetRasterYSize();
  ret.first.nCols = (size_t)ds->GetRasterXSize();

  return ret;
};

std::optional<std::string> Sampler::ensureCache() {
  std::vector<SampleInfo *> cacheGenQueue(infos.size(), nullptr);
  size_t cacheGenQueueIndex = 0;

  omp_lock_t queueGenErrorLock;
  std::string queueGenError = "";
  omp_init_lock(&queueGenErrorLock);

  omp_set_num_threads(sampleOptions.nCacheQueryThreads);
#pragma omp parallel for
  for (size_t i = 0; i < infos.size(); i++) {
    size_t threadNum = omp_get_thread_num();
    sqlite3 *conn = connectionPool[threadNum];

    ComputationCache cache = {};
    bool hasCache = false;
    auto err = getCacheEntry(conn, infos[i], &cache, &hasCache);

    if (err) {
      omp_set_lock(&queueGenErrorLock);
      if (queueGenError.size() != 0) {
        queueGenError += ", ";
      }
      queueGenError += infos[i].productName + ": " + err.value() + "\n";
      omp_unset_lock(&queueGenErrorLock);

      continue;
    }

    if (!hasCache || !cacheValid(infos[i], cache)) {
      size_t queueIndex;
#pragma omp atomic capture
      queueIndex = cacheGenQueueIndex++;
      cacheGenQueue[queueIndex] = &infos[i];

      if (hasCache) {
        freeSampleCache(cache.sampleCache);
      }

      continue;
    }

    freeSampleCache(cache.sampleCache);
  }
  omp_destroy_lock(&queueGenErrorLock);

  if (!queueGenError.empty()) {
    return "queuegen errors: " + queueGenError;
  }

  omp_lock_t cacheGenErrorLock;
  std::string cacheGenError = "";
  omp_init_lock(&cacheGenErrorLock);

  std::cout << "num teams: " << omp_get_num_teams() << std::endl;
  omp_set_num_threads(sampleOptions.nCacheGenThreads);
#pragma omp parallel for
  for (size_t i = 0; i < cacheGenQueueIndex; i++) {
    auto [sampleCache, err] =
        genSampleCache(*cacheGenQueue[i], cacheGenOptions);

    if (!sampleCache.has_value()) {
      omp_set_lock(&cacheGenErrorLock);
      if (cacheGenError.size() != 0) {
        cacheGenError += ", ";
      }

      cacheGenError += cacheGenQueue[i]->productName + ": " + err;

      omp_unset_lock(&cacheGenErrorLock);

      continue;
    }

    auto maxDims = getMaxResolution(cacheGenQueue[i]->path.string());
    if (!maxDims.has_value()) {
      omp_set_lock(&cacheGenErrorLock);
      if (cacheGenError.size() != 0) {
        cacheGenError += ", ";
      }

      cacheGenError += cacheGenQueue[i]->productName +
                       ": failed to retreive maximum dimensions---either this "
                       "is a filesystem issue or the dimension scaling is "
                       "invalid in this product\n";

      omp_unset_lock(&cacheGenErrorLock);

      freeSampleCache(sampleCache.value());
      continue;
    }

    struct stat st;
    int ret = stat(cacheGenQueue[i]->path.c_str(), &st);
    uint64_t lastModTime = st.st_mtim.tv_sec;

    ComputationCache cache = {.sampleCache = std::move(sampleCache.value()),
                              .maxDimX = maxDims->first,
                              .maxDimY = maxDims->second,
                              .productName =
                                  cacheGenQueue[i]->productName.c_str(),
                              .unixModTime = lastModTime};

    // write cache entry
    std::optional<std::string> writeErr = std::nullopt;
#pragma omp critical(WRITE_CACHE)
    writeErr = writeCacheEntry(connectionPool[omp_get_thread_num()], cache);

    if (writeErr) {
      omp_set_lock(&cacheGenErrorLock);
      cacheGenError += cacheGenQueue[i]->productName +
                       " cache write error: " + writeErr.value() + "\n";
      omp_unset_lock(&cacheGenErrorLock);
    }

    freeSampleCache(sampleCache.value());
  }
  omp_destroy_lock(&cacheGenErrorLock);

  if (cacheGenError.size() != 0) {
    return cacheGenError;
  }

  return std::nullopt;
} // namespace sats

std::vector<std::vector<float *>> Sampler::randomSampleV2(size_t n) {
  // 1. get files to sample (synchronous)
  // std::set<std::string> products;
  // std::set<std::pair<std::string, std::pair<
  struct SampleIndex {
    SampleInfo *info;
    size_t sampleCoordIndex;
    ComputationCache *cache;

    inline bool operator<(const SampleIndex &other) const {
      if (info < other.info) {
        return true;
      } else if (info > other.info) {
        return false;
      }

      return sampleCoordIndex < other.sampleCoordIndex;
    }
  };

  std::set<SampleIndex> samples;
  std::unordered_map<SampleInfo *, ComputationCache> caches;

  // cursed loop index modification lol
  for (size_t _i = 1; _i < n + 1; _i++) {
    size_t infoIndex = (size_t)(std::rand() % infos.size());
    SampleInfo *info = &infos[infoIndex];

    if (!caches.contains(info)) {
      caches[info] = {};

      ComputationCache *cache = &caches[info];
      bool haveCache;

      auto err = getCacheEntry(connectionPool[0], *info, cache, &haveCache);

      if (err) {
        std::cout << "randomSampleV2: cache read error: " << err.value()
                  << std::endl;
        return std::vector<std::vector<float *>>{};
      }

      if (!haveCache) {
        std::cout << "randomSampleV2: cache not present for "
                  << info->productName << std::endl;
        return std::vector<std::vector<float *>>{};
      }
    }
    ComputationCache *cache = &caches[info];

    size_t sampleNOKIndex = 0;
    if (cache->sampleCache.nOK) {
      sampleNOKIndex = (size_t)(std::rand() % cache->sampleCache.nOK);
    }

    auto sampleIndex = computeSampleIndex(sampleNOKIndex, cache->sampleCache);

    SampleIndex index = {
        .info = info,
        .sampleCoordIndex = sampleIndex,
        .cache = cache,
    };

    if (samples.contains(index) || !cache->sampleCache.nOK) {
      _i--;
    } else {
      samples.insert(index);
    }
  }

  // 2. sample (threaded)
  std::vector<std::vector<float *>> reads;

#pragma omp parallel
  {
#pragma omp single
    for (const auto &sample : samples) {
#pragma omp task
      {
        SampleIndex sampleInfo = sample;
        const auto &info = *sampleInfo.info;
        SampleCache *cache = &sampleInfo.cache->sampleCache;

        int scalingFactor = info.maxDimX / cache->nCols;
        assert(scalingFactor);
        assert(scalingFactor == info.maxDimY / cache->nRows);

        size_t sampleIndexX =
            sample.sampleCoordIndex % sample.cache->sampleCache.nCols;
        size_t sampleIndexY =
            sample.sampleCoordIndex / sample.cache->sampleCache.nCols;

        sampleIndexX *= scalingFactor;
        sampleIndexY *= scalingFactor;

        std::cout << "final path: " << info.path << std::endl;

        std::vector<float *> bands;
        for (const auto &flavor : flavors) {
          std::string dsPath =
              "vrt:///vsizip/" +
              (std::filesystem::canonical(info.path) /
               (info.productName + "-" + flavor + ".tif" +
                std::format("?outsize={},{}", info.maxDimX, info.maxDimY)))
                  .string();

          GDALDatasetUniquePtr ds =
              GDALDatasetUniquePtr(GDALDataset::FromHandle(
                  GDALOpenShared(dsPath.c_str(), GDALAccess::GA_ReadOnly)));

          if (!ds) {
            std::cout << "failed to open " << dsPath << std::endl;

            for (const auto &band : bands) {
              free(band);
            }

            continue;

            // return
          }

          bool readError = false;
          for (const auto &band : ds->GetBands()) {
            std::cout << "read band " << band->GetBand() << " of "
                      << info.productName << " of flavor " << flavor
                      << std::format("({}, {}, {}, {})", sampleIndexX,
                                     sampleIndexY, cacheGenOptions.sampleDim,
                                     cacheGenOptions.sampleDim)
                      << std::endl;
            float *data =
                (float *)malloc(sizeof(float) * cacheGenOptions.sampleDim *
                                cacheGenOptions.sampleDim);
            CPLErr e = band->RasterIO(
                GF_Read, sampleIndexX, sampleIndexY, cacheGenOptions.sampleDim,
                cacheGenOptions.sampleDim, data, cacheGenOptions.sampleDim,
                cacheGenOptions.sampleDim, GDT_Float32, 0, 0);

            if (e) {
              std::cout << "failed to read band " << band->GetBand() << " of "
                        << info.productName << " of flavor " << flavor
                        << std::format("({}, {}, {}, {})", sampleIndexX,
                                       sampleIndexY, cacheGenOptions.sampleDim,
                                       cacheGenOptions.sampleDim)
                        << std::endl;
              free(data);

              readError = true;
              break;
            }

            if (!readError) {
              bands.push_back(data);
            } else {
              for (const auto &band : bands) {
                free(band);
              }

              bands.clear();
            }
          }
          // ds->Close();
        }

        freeSampleCache(*cache);

        if (!bands.empty()) {
#pragma omp critical(UPDATE_BANDS)
          reads.push_back(bands);
        }
      }
    }
  }
  // 3. return (join)
  return reads;
}

Sampler::~Sampler() {
  for (const auto &connection : connectionPool) {
    sqlite3_close_v2(connection);
  }
}

// std::vector<float *> Sampler::randomSample() {
//
//   int fileIndex = std::rand() % infos.size();
//   const auto &info = infos[fileIndex];
//
//   size_t sampleOKIndex = std::rand() % info.cache->nOK;
//
//   int sampleIndex = computeSampleIndex(sampleOKIndex, info.cache.value());
//
//   int scalingFactor = info.maxDimX / info.cache->nCols;
//   assert(scalingFactor);
//   assert(scalingFactor == info.maxDimY / info.cache->nRows);
//
//   std::cout << "scaling factor: " << scalingFactor << std::endl;
//
//   std::cout << "row: " << sampleIndex % info.cache->nCols << std::endl;
//   size_t sampleIndexX = (sampleIndex % info.cache->nCols) * scalingFactor;
//   size_t sampleIndexY = (sampleIndex / info.cache->nRows) * scalingFactor;
//
//   std::cout << "final path: " << info.path << std::endl;
//
//   std::vector<float *> bands;
//   for (const auto &flavor : flavors) {
//     std::string dsPath =
//         "vrt:///vsizip/" +
//         (std::filesystem::canonical(info.path) /
//          (info.productName + "-" + flavor + ".tif" +
//           std::format("?outsize={},{}", info.maxDimX, info.maxDimY)))
//             .string();
//
//     GDALDataset *ds = GDALDataset::FromHandle(
//         GDALOpenShared(dsPath.c_str(), GDALAccess::GA_ReadOnly));
//
//     if (!ds) {
//       std::cout << "failed to open " << dsPath << std::endl;
//
//       for (const auto &band : bands) {
//         free(band);
//       }
//
//       return std::vector<float *>();
//     }
//
//     for (const auto &band : ds->GetBands()) {
//       std::cout << "read band " << band->GetBand() << " of " <<
//       info.productName
//                 << " of flavor " << flavor
//                 << std::format(
//                        "({}, {}, {}, {})", sampleIndex %
//                        ds->GetRasterXSize(), sampleIndex /
//                        ds->GetRasterXSize(), cacheGenOptions.sampleDim,
//                        cacheGenOptions.sampleDim)
//                 << std::endl;
//       float *data = (float *)malloc(sizeof(float) *
//       cacheGenOptions.sampleDim
//       *
//                                     cacheGenOptions.sampleDim);
//       CPLErr e = band->RasterIO(
//           GF_Read, sampleIndexX, sampleIndexY, cacheGenOptions.sampleDim,
//           cacheGenOptions.sampleDim, data, cacheGenOptions.sampleDim,
//           cacheGenOptions.sampleDim, GDT_Float32, 0, 0);
//
//       if (e) {
//         std::cout << "failed to read band " << band->GetBand() << " of "
//                   << info.productName << " of flavor " << flavor
//                   << std::format(
//                          "({}, {}, {}, {})", sampleIndex %
//                          ds->GetRasterXSize(), sampleIndex /
//                          ds->GetRasterXSize(), cacheGenOptions.sampleDim,
//                          cacheGenOptions.sampleDim)
//                   << std::endl;
//         free(data);
//
//         for (const auto &band : bands) {
//           free(band);
//         }
//
//         return std::vector<float *>();
//       }
//
//       bands.push_back(data);
//     }
//
//     // ds->Close();
//   }
//
//   return bands;
// }

// return std::make_pair(, "");
} // namespace sats
