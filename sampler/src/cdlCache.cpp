#include "cdlCache.h"
#include <cpl_error.h>
#include <filesystem>
#include <format>

#include <gdal.h>
#include <gdalwarper.h>
#include <utility>
#include <vrtdataset.h>

namespace sats {

uint8_t *CDLCache::read(const std::filesystem::path &path,
                        const OGRSpatialReference *srs,
                        CDLCache::ProjWin projWin, size_t *oDimX,
                        size_t *oDimY) {

  // https://gdal.org/en/stable/drivers/raster/vrt.html#processed-dataset-vrt
  std::string openPath =
      std::format("{}", std::filesystem::absolute(path).string());

  GDALDatasetH srcDS = GDALOpen(openPath.c_str(), GA_ReadOnly);
  GDALDatasetH warpedDS =
      GDALAutoCreateWarpedVRT(srcDS, NULL, srs->exportToWkt().c_str(),
                              GDALResampleAlg::GRA_NearestNeighbour, 0.0, NULL);

  // https://gdal.org/en/stable/tutorials/geotransforms_tut.html
  // X_geo = GT(0) + X_pixel * GT(1) + Y_line * GT(2)
  // Y_geo = GT(3) + X_pixel * GT(4) + Y_line * GT(5)
  //
  // Thus,
  // X_pixel = GT_INV(0) + X_geo * GT_INV(1) + Y_geo * GT_INV(2)
  // Y_line  = GT_INV(3) + X_geo * GT_INV(4) + Y_geo * GT_INV(5)

  double dstGeoTransform[6];
  GDALGetGeoTransform(warpedDS, dstGeoTransform);
  double dstInvGeoTransform[6];
  if (!GDALInvGeoTransform(dstGeoTransform, dstGeoTransform)) {
    std::cout << "Failed on GeoTransform" << std::endl;
    return nullptr;
  }

  double x1, y1;
  double x2, y2;
  double x3, y3;
  double x4, y4;

  GDALApplyGeoTransform(dstInvGeoTransform, projWin.xmin, projWin.ymin, &x1,
                        &y1);
  GDALApplyGeoTransform(dstInvGeoTransform, projWin.xmax, projWin.ymin, &x2,
                        &y2);
  GDALApplyGeoTransform(dstInvGeoTransform, projWin.xmin, projWin.ymax, &x3,
                        &y3);
  GDALApplyGeoTransform(dstInvGeoTransform, projWin.xmax, projWin.ymax, &x4,
                        &y4);

  // Determine top left
  //
  // This seems to fit our needs:
  // https://stackoverflow.com/a/2819309
  std::pair<double, double> minPixel =
      std::min({std::make_pair(x1, y1), std::make_pair(x2, y2),
                std::make_pair(x3, y3), std::make_pair(x4, y4)});

  // Determine bottom right
  std::pair<double, double> maxPixel =
      std::max({std::make_pair(x1, y1), std::make_pair(x2, y2),
                std::make_pair(x3, y3), std::make_pair(x4, y4)});

  int startX = (int)minPixel.first;
  int startY = (int)minPixel.second;
  int endX = (int)maxPixel.first;
  int endY = (int)maxPixel.second;

  if (startX < 0 || endX < 0 || endY < 0 || startY < 0) {
    std::cout << "read bounds are out of bounds" << std::endl;

    GDALReleaseDataset(warpedDS);
    GDALReleaseDataset(srcDS);

    return nullptr;
  }

  if (startX > endX || startY > endY) {
    std::cout << "start coordinates greater than end coordinates" << std::endl;

    GDALReleaseDataset(warpedDS);
    GDALReleaseDataset(srcDS);

    return nullptr;
  }

  size_t dimX = endX - startX;
  size_t dimY = endY - startY;

  GDALRasterBandH band = GDALGetRasterBand(warpedDS, 1);
  if (!band) {
    std::cout << "Could not get first band from dataset!" << std::endl;

    GDALReleaseDataset(warpedDS);
    GDALReleaseDataset(srcDS);

    return nullptr;
  }

  uint8_t *mem = (uint8_t *)malloc(dimX * dimY);

  CPLErr err = GDALRasterIO(band, GF_Read, startX, startY, dimX, dimY, mem,
                            dimX, dimY, GDT_Byte, 0, 0);
  if (err) {
    std::cout << "Raster IO failed!" << std::endl;

    GDALReleaseDataset(warpedDS);
    GDALReleaseDataset(srcDS);

    return nullptr;
  }

  *oDimX = dimX;
  *oDimY = dimY;

  GDALReleaseDataset(warpedDS);
  GDALReleaseDataset(srcDS);

  return mem;
}

} // namespace sats
