#pragma once

#include <filesystem>
#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <iostream>
#include <ogr_spatialref.h>
#include <streambuf>
#include <unordered_map>

namespace sats::cdl {

struct ProjWin {
  double xmin, xmax, ymin, ymax;
};

std::vector<float> read(const std::string &openPath, const char *crs,
                        ProjWin projWin, size_t dimX, size_t dimY);

} // namespace sats::cdl
