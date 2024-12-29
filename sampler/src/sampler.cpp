#include "sampler.h"
#include "cuda/mapgen.h"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <regex>

#include <gdal.h>
#include <gdal_priv.h>

#include <iostream>
#include <utility>

namespace sats {

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

    infos.push_back({
        .path = path.path(),
        .year = year,
        .month = month,
        .day = day,
        .tileName = tileName,
        .cache = std::nullopt,
    });

    std::cout << year << " " << month << " " << day << " " << tileName << ": "
              << path << std::endl;

    if (preproc) {
      auto [val, err] =
          genCache(std::filesystem::canonical(path), cacheGenOptions);

      if (!val) {
        std::cout << "load error: " << err << std::endl;
      } else {
        std::cout << "nOK: " << val->nOK << std::endl;
      }
    }
  }
}

std::pair<std::optional<Sampler::SampleCache>, std::string>
Sampler::genCache(const std::filesystem::path &path,
                  Sampler::SampleCacheGenOptions genOptions) {
  std::regex subRe("S2[A-Z](.*)\\.SAFE");
  std::smatch subMatch;

  std::string fname = path.filename().string();
  assert(std::regex_search(fname, subMatch, subRe));

  std::filesystem::path mskProductPath =
      path / std::filesystem::path(subMatch.str() + "-MSK.tif");

  GDALDataset *ds =
      GDALDataset::Open(("/vsizip/" + mskProductPath.string()).c_str());

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
  generateSampleMap((unsigned char *)bands, nBands - 2,
                    (unsigned char *)((char *)bands + cldIdx * nPixels),
                    cacheGenOptions.cldMax,
                    (unsigned char *)((char *)bands + snwIdx * nPixels),
                    cacheGenOptions.snwMax, (unsigned char *)stage,
                    ds->GetRasterXSize(), ds->GetRasterYSize(),
                    cacheGenOptions.sampleDim, cacheGenOptions.minOKPercentage);

  size_t nPixelsCondensed =
      (nPixels / sizeof(uint8_t)) + (nPixels % sizeof(uint8_t) != 0);
  uint8_t *condensed = (uint8_t *)malloc(
      sizeof(uint8_t) * ds->GetRasterXSize() * ds->GetRasterYSize());

  size_t okTotal = 0;
  // for (int i = 0; i < nPixels; i++) {
  //   if (((uint8_t *)stage)[i])
  //     okTotal++;
  // }
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

  return std::make_pair(cache, "");
};

// return std::make_pair(, "");
} // namespace sats
