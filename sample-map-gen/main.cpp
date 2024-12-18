#include <algorithm>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cstddef>
#include <cstdlib>
#include <execution>
#include <future>
#include <gdal.h>
#include <iostream>

#include <gdal_priv.h>
#include <gdal_utils.h>

#include "cuda/mapgen.h"
#include "cuda/maskJoin.h"
#include <cuda_runtime.h>
#include <unordered_set>

#include "third_party/argparse.hpp"

// 1. Build valid pixel masks from warped DETFOO masks
// 2. Run buildSampleMask on resulting map taking in warped cloud and snow/ice
// probability

struct Args : public argparse::Args {
  size_t &sampleDim =
      kwarg("sd,sample-dim", "Dimension of box to sample").set_default(256);
  std::vector<std::string> &detfooMasks =
      kwarg("dfm,detfoo-masks", "The list of detector footprint masks to use")
          .multi_argument();
  std::string &cldMask =
      kwarg("cld,cloud-mask", "The path to the cloud probability mask to use");
  std::string &snwMask =
      kwarg("snw,snow-mask", "The path to the snow probability mask to use");
  uint8_t &snwProbMax = kwarg("snwm,snow-max", "The maximum snow probability");
  uint8_t &cldProbMax =
      kwarg("cldm,cloud-max", "The maximum cloud probability");
};

int main(int argc, const char **argv) {
  auto args = argparse::parse<Args>(argc, argv);

  GDALAllRegister();

  std::vector<GDALDataset *> detfooMasks;
  for (const auto &maskPath : args.detfooMasks) {
    std::cout << "opening " << std::filesystem::path(maskPath) << std::endl;
    detfooMasks.push_back(GDALDataset::Open(maskPath.c_str(), GA_ReadOnly));

    if (!detfooMasks.back()) {
      std::cout << "Failed to open \"" << maskPath << "\"" << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  size_t dsXSize = 0, dsYSize = 0;
  for (const auto &ds : detfooMasks) {
    size_t newXSize = ds->GetRasterXSize(), newYSize = ds->GetRasterYSize();

    std::cout << newXSize << ", " << newYSize << std::endl;

    size_t maxX = std::max(newXSize, dsXSize);
    size_t minX = std::min(newXSize, dsXSize);

    size_t maxY = std::max(newYSize, dsYSize);
    size_t minY = std::min(newYSize, dsYSize);

    if (minX && maxX % minX) {
      std::cout << "dataset x dimension " << minX << " is not a factor of "
                << maxX << std::endl;
      return EXIT_FAILURE;
    }

    if (minY && maxY % minY) {
      std::cout << "dataset y dimension " << minY << " is not a factor of "
                << maxY << std::endl;
      return EXIT_FAILURE;
    }

    dsXSize = maxX;
    dsYSize = maxY;
  }

  std::cout << "ds size: " << dsXSize << ", " << dsYSize << std::endl;

  unsigned char *masks = (unsigned char *)malloc(
      sizeof(unsigned char) * dsXSize * dsYSize * detfooMasks.size());

  for (size_t i = 0; i < detfooMasks.size(); i++) {
    const auto &ds = detfooMasks[i];

    std::cout << "read: " << i << std::endl;

    CPLErr readErr = ds->GetRasterBand(1)->RasterIO(
        GDALRWFlag::GF_Read, 0, 0, ds->GetRasterXSize(), ds->GetRasterYSize(),
        masks + i * dsXSize * dsYSize * sizeof(unsigned char), dsXSize, dsYSize,
        GDALDataType::GDT_Byte, 0, 0);

    if (readErr) {
      std::cout << "readErr: " << readErr << std::endl;
    }
  }

  unsigned char *cldMask =
      (unsigned char *)malloc(sizeof(unsigned char) * dsXSize * dsYSize);
  {
    auto dsPtr = GDALDatasetUniquePtr(
        GDALDataset::Open(args.cldMask.c_str(), GA_ReadOnly));

    std::cout << "dsPtr: " << dsPtr.get() << std::endl;
    if (!dsPtr) {
      std::cout << "Failed to open \"" << args.cldMask << "\"" << std::endl;
      exit(EXIT_FAILURE);
    }

    CPLErr readErr = dsPtr->GetRasterBand(1)->RasterIO(
        GDALRWFlag::GF_Read, 0, 0, dsPtr->GetRasterXSize(),
        dsPtr->GetRasterYSize(), cldMask, dsXSize, dsYSize,
        GDALDataType::GDT_Byte, 0, 0);

    if (readErr) {
      std::cout << "Failed to read \"" << args.cldMask << "\"" << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  unsigned char *snwMask =
      (unsigned char *)malloc(sizeof(unsigned char) * dsXSize * dsYSize);
  {
    auto dsPtr = GDALDatasetUniquePtr(
        GDALDataset::Open(args.snwMask.c_str(), GA_ReadOnly));
    std::cout << "dsPtr: " << dsPtr.get() << std::endl;

    if (!dsPtr) {
      std::cout << "Failed to open \"" << args.snwMask << "\"" << std::endl;
      exit(EXIT_FAILURE);
    }

    CPLErr readErr = dsPtr->GetRasterBand(1)->RasterIO(
        GDALRWFlag::GF_Read, 0, 0, dsPtr->GetRasterXSize(),
        dsPtr->GetRasterYSize(), snwMask, dsXSize, dsYSize,
        GDALDataType::GDT_Byte, 0, 0);

    if (readErr) {
      std::cout << "Failed to read \"" << args.snwMask << "\"" << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  unsigned char *outMask =
      (unsigned char *)malloc(sizeof(unsigned char) * dsXSize * dsYSize);
  // joinDetfooMasks(masks, outMask, dsXSize, dsYSize, detfooMasks.size());
  joinMasks(masks, detfooMasks.size(), cldMask, args.cldProbMax, snwMask,
            args.snwProbMax, outMask, dsXSize, dsYSize);

  GDALDriver *gtiffDriver = GDALDriver::FromHandle(GDALGetDriverByName("GTiff"));
  GDALDataset *outDS =
      gtiffDriver->Create("out.tif", dsXSize, dsYSize, 1, GDT_Byte, NULL);
  // GDALDataset *outDS =
  //     cogDriver->CreateCopy("out.tif", detfooMasks[0], FALSE, NULL, NULL,
  //     NULL);

  std::cout << "size: " << outDS->GetRasterXSize() << ", "
            << outDS->GetRasterYSize() << std::endl;
  CPLErr err = outDS->GetRasterBand(1)->RasterIO(
      GF_Write, 0, 0, outDS->GetRasterXSize(), outDS->GetRasterYSize(),
      (void *)outMask, dsXSize, dsYSize, GDALDataType::GDT_Byte, 0, 0);

  if (err) {
    std::cout << "GDAL Error: " << err << std::endl;
    exit(EXIT_FAILURE);
  }

  outDS->Close();

  std::unordered_set<unsigned char> vals;
  for (size_t i = 0; i < dsXSize * dsYSize; i++) {
    vals.insert(outMask[i]);
  }

  for (const auto &val : vals) {
    std::cout << "val: " << (int)val << std::endl;
  }
}
