#pragma once

#include <filesystem>
#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <iostream>
#include <ogr_spatialref.h>
#include <streambuf>
#include <unordered_map>

namespace sats {
class CDLCache {
public:
  CDLCache() {}

  struct ProjWin {
    double xmin, xmax, ymin, ymax;
  };

  uint8_t *read(const std::filesystem::path &path,
                const OGRSpatialReference *srs, ProjWin projWin, size_t *oDimX,
                size_t *oDimY);

private:
  // struct PathEqual {
  //   constexpr bool operator()(const std::filesystem::path &a,
  //                             const std::filesystem::path &b) {
  //     return std::filesystem::equivalent(a, b);
  //   }
  // };
  //
  // std::unordered_map<std::filesystem::path, GDALDataset *,
  //                    std::hash<std::filesystem::path>, PathEqual>
  //     filemap;
};
} // namespace sats
