#include <cpl_conv.h>
#include <cpl_error.h>
#include <cstring>
#include <gdal.h>

#include <gdal_priv.h>
#include <gdal_utils.h>

int main(int argc, const char **argv) {
  GDALAllRegister();
  GDALDatasetUniquePtr dsPtr = GDALDatasetUniquePtr(GDALDataset::FromHandle(
      GDALDataset::Open(argv[1], GDAL_OF_RASTER | GDAL_OF_UPDATE)));

  for (int i = 2; i < argc; i++) {
    dsPtr->GetRasterBand(i - 1)->SetDescription(argv[i]);
  }
}
