#pragma once

#include <filesystem>
#include <gdal.h>
#include <gdal_priv.h>
#include <iostream>
#include <ogr_spatialref.h>
#include <gdal_utils.h>

namespace sats {
class CDLCache {
public:
  CDLCache() = delete;

  CDLCache(const std::filesystem::path &path, const std::string &crs) {
    GDALDataset *originalDS = GDALDataset::Open(path.c_str());

    if (!originalDS) {
      std::cout << "could not open cdl path: " << path << std::endl;
    }

    // I don't need to close this thing, do I?
    GDALDriver *driver = GDALDriver::FromHandle(GDALGetDriverByName("VRT"));

    // Construct virtual DS with CRS warped to the target
    GDALDataset *vrtDS =
        driver->CreateCopy("", originalDS, 0, NULL, NULL, NULL);

    vrtDS->SetProjection(crs.c_str());


    GDALTranslate(const char *pszDestFilename, GDALDatasetH hSrcDataset, const GDALTranslateOptions *psOptions, int *pbUsageError);
  }
};
} // namespace sats
