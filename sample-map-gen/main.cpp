#include <algorithm>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cstddef>
#include <execution>
#include <future>
#include <gdal.h>
#include <iostream>

#include <gdal_priv.h>
#include <gdal_utils.h>

#include "cuda/mapgen.h"
#include <cuda_runtime.h>

void readBand(int bandNum, GDALDataset *dsPtr, int dsXSize, int dsYSize,
              float *bandData) {}

int main(int argc, const char **argv) {
  GDALAllRegister();
  GDALDatasetUniquePtr dsPtr = GDALDatasetUniquePtr(
      GDALDataset::FromHandle(GDALDataset::Open(argv[1], GDAL_OF_RASTER)));

  int bboxSize = 256;

  size_t dsXSize = dsPtr->GetRasterXSize();
  size_t dsYSize = dsPtr->GetRasterYSize();

  std::cout << "dataset is " << dsXSize << "x" << dsYSize << std::endl;
  dsXSize /= 2;
  dsYSize /= 2;

  float *bandData = (float *)malloc(sizeof(float) * dsXSize * dsYSize * 10);
  std::cout << "CPLMalloc: " << sizeof(float) * dsXSize * dsYSize * 10
            << std::endl;

  std::cout << "band data addr: " << bandData << std::endl;

  std::vector<std::thread> readThreads;
  for (int bandNum = 1; bandNum <= 10; bandNum++) {
    std::cout << "reading band " << bandNum << std::endl;
    GDALRasterBand *pBand = dsPtr->GetRasterBand(bandNum);

    float *endPtr = bandData + 10 * dsXSize * dsYSize;
    std::cout << "dsSize: " << dsXSize << ", " << dsYSize << std::endl;
    std::cout << "band size: " << pBand->GetXSize() / 4 << ", "
              << pBand->GetYSize() / 4 << std::endl;
    CPLErr e = pBand->ReadRaster(bandData + (dsXSize * dsYSize) * (bandNum - 1),
                                 dsXSize * dsYSize, 0, 0, dsXSize, dsYSize);

    if (e) {
      std::cout << "read band " << bandNum << " error: " << e << std::endl;
      return 1;
    }
    std::cout << "finished reading band " << bandNum << std::endl;
  }

  for (std::thread &thread : readThreads) {
    thread.join();
  }

  std::cout << "running kernel..." << std::endl;

  buildSampleMap((float ***)bandData, nullptr, dsXSize, dsYSize, 10, bboxSize);

  free(bandData);

  // // clang-format off
  // const char *args[] = {
  //     "-b", "1",
  //     "-b", "2",
  //     "-b", "3",
  //     "-b", "4",
  //     "-b", "5",
  //     "-b", "6",
  //     "-b", "7",
  //     "-b", "8",
  //     "-b", "9",
  //     "-b", "10",
  //     NULL,
  // };
  // // clang-format on
  //
  // // sus
  // GDALFootprintOptions *footprintOptions =
  //     GDALFootprintOptionsNew((char **)args, NULL);
  //
  // int error = 0;
  // GDALDatasetUniquePtr vecDSPtr =
  // GDALDatasetUniquePtr(GDALDataset::FromHandle(
  //     ((GDALDriver *)(GDALGetDriverByName("GeoJSON")))
  //         ->Create("bruh", 0, 0, 0, GDT_Unknown, NULL)));
  // GDALFootprint(NULL, vecDSPtr.get(), dsPtr.get(), footprintOptions, &error);
  //
  // if (error) {
  //   std::cout << "error: " << error << std::endl;
  // }
  //
  // GDALFootprintOptionsFree(footprintOptions);
}
