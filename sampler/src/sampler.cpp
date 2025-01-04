#include "sampler.h"
#include "cpu/mapgen.h"
#include "cuda/mapgen.h"
#include "signal.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iterator>
#include <optional>
#include <regex>

#include <gdal.h>
#include <gdal_priv.h>

#include <iostream>
#include <omp.h>
#include <utility>

namespace sats {

static const std::string flavors[] = {"HIRES", "LOWRES"};

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

  GDALDataset *ds = GDALDataset::Open(productPath.value().c_str());

  if (!ds) {
    std::cout << "could not open ds to get dims" << std::endl;

    return false;
  }

  *xDim = ds->GetRasterXSize();
  *yDim = ds->GetRasterYSize();

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

Sampler::Sampler(const std::filesystem::path &dataDir,
                 SampleCacheGenOptions cacheGenOptions,
                 std::optional<DateRange> dateRange, bool preproc) {
  GDALAllRegister();

  this->cacheGenOptions = cacheGenOptions;

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
        .cache = std::nullopt,
    });

    std::cout << year << " " << month << " " << day << " " << tileName << ": "
              << path << std::endl;
  }

  omp_set_dynamic(0);
  omp_set_num_threads(16);
#pragma omp parallel for
  for (int i = 0; i < infos.size(); i++) {
    auto &info = infos[i];
    if (preproc) {
      auto [val, err] = genCache(info, cacheGenOptions);

      std::cout << info.path << std::endl;
      if (!val.has_value()) {
        std::cout << "load error: " << err << std::endl;
      } else {
        std::cout << "nOK: " << val->nOK << std::endl;
        std::cout << "xDim, yDim: " << val->nCols << " " << val->nRows
                  << std::endl;
      }

      info.cache = std::move(val);
    }
  }
}

std::pair<std::optional<Sampler::SampleCache>, std::string>
Sampler::genCache(const SampleInfo &info,
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
       std::format("?outsize={},{}", info.maxDimX, info.maxDimY))
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

  uint8_t **stage = (uint8_t **)malloc(sizeof(uint8_t) * ds->GetRasterXSize() *
                                       ds->GetRasterYSize());

  size_t nPixels = ds->GetRasterXSize() * ds->GetRasterYSize();

#if 0 && HAS_CUDA
  cudaproc::generateSampleMap(
      (unsigned char *)bands, nBands - 2,
      (unsigned char *)((char *)bands + cldIdx * nPixels),
      cacheGenOptions.cldMax,
      (unsigned char *)((char *)bands + snwIdx * nPixels),
      cacheGenOptions.snwMax, (unsigned char *)stage, ds->GetRasterXSize(),
      ds->GetRasterYSize(), cacheGenOptions.sampleDim,
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

  size_t nPixelsCondensed =
      (nPixels / sizeof(uint8_t)) + (nPixels % sizeof(uint8_t) != 0);
  // ensure 64 bit size multiple for iteration purposes
  nPixelsCondensed = nPixelsCondensed + (64 - (nPixelsCondensed % 64)) *
                                            (nPixelsCondensed % 64 != 0);

  uint8_t *condensed = (uint8_t *)malloc(nPixelsCondensed);
  // zero memory, could use calloc instead
  memset(condensed, 0, nPixelsCondensed);

  size_t okTotal = 0;
  for (size_t i = 0; i < nPixelsCondensed; i++) {
    for (int j = 0; j < 8 && (i * 8 + j) < nPixels; j++) {
      int ok = (((uint8_t *)stage)[i * 8 + j] != 0);
      okTotal += ok;
      condensed[i] |= ok << j;
    }
  }

  free(stage);
  free(bands);
  ds->Close();

  SampleCache cache = {
      .bitrange = condensed,
      .size = nPixelsCondensed,
      .nOK = okTotal,
      .nRows = (size_t)ds->GetRasterYSize(),
      .nCols = (size_t)ds->GetRasterXSize(),
  };

  auto ret = std::make_pair(SampleCache{}, "");
  ret.first = cache;

  return ret;
};

std::vector<float *> Sampler::randomSample() {
  // need to revamp this
  std::srand(time(NULL));

  int fileIndex = std::rand() % infos.size();
  const auto &info = infos[fileIndex];

  int sampleOKIndex = std::rand() % info.cache->nOK;

  int sampleIndex = 0;
  {
    int currentOKIndex = 0;
    int currentWordIndex = 0;
    while (currentOKIndex < sampleOKIndex) {
      currentOKIndex +=
          std::popcount(((uint64_t *)info.cache->bitrange)[currentWordIndex++]);
    }

    if (currentOKIndex != sampleOKIndex) {
      currentOKIndex -=
          std::popcount(((uint64_t *)info.cache->bitrange)[--currentWordIndex]);

      int nthBit = sampleOKIndex - currentOKIndex;
      assert(nthBit > 0);
      assert(nthBit < 64);

      int j = 0;
      int i = 0;
      while (i < 64 && j < nthBit) {
        if ((1 << i) & ((uint64_t *)info.cache->bitrange)[currentWordIndex]) {
          j++;
        }

        i++;
      }

      assert(j == nthBit);
      assert(i >= nthBit);

      sampleIndex = currentWordIndex * sizeof(uint64_t) + j;
      assert(sampleIndex > 0 &&
             sampleIndex < (info.cache->nRows - cacheGenOptions.sampleDim) *
                               (info.cache->nCols - cacheGenOptions.sampleDim));
    }
  }

  std::cout << "final path: " << info.path << std::endl;

  std::vector<float *> bands;
  for (const auto &flavor : flavors) {
    std::string dsPath =
        "vrt:///vsizip/" +
        (std::filesystem::canonical(info.path) /
         (info.productName + "-" + flavor + ".tif" +
          std::format("?outsize={},{}", info.maxDimX, info.maxDimX)))
            .string();

    GDALDataset *ds = GDALDataset::Open(dsPath.c_str(), GF_Read);

    if (!ds) {
      std::cout << "failed to open " << dsPath << std::endl;

      for (const auto &band : bands) {
        free(band);
      }

      return std::vector<float *>();
    }

    for (const auto &band : ds->GetBands()) {
      float *data = (float *)malloc(sizeof(float) * cacheGenOptions.sampleDim *
                                    cacheGenOptions.sampleDim);
      CPLErr e = band->RasterIO(
          GF_Read, sampleIndex % ds->GetRasterXSize(),
          sampleIndex / ds->GetRasterXSize(), cacheGenOptions.sampleDim,
          cacheGenOptions.sampleDim, data, cacheGenOptions.sampleDim,
          cacheGenOptions.sampleDim, GDT_Float32, 0, 0);

      if (e) {
        std::cout << "failed to read band " << band->GetBand() << " of "
                  << info.productName << " of flavor " << flavor << std::endl;
        free(data);

        for (const auto &band : bands) {
          free(band);
        }

        return std::vector<float *>();
      }

      bands.push_back(data);
    }

    ds->Close();
  }

  return bands;
}

// return std::make_pair(, "");
} // namespace sats
