#include "load.h"
#include <cpl_conv.h>
#include <filesystem>
#include <gdal.h>
#include <iostream>

#include <cpl_string.h>
#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <map>
#include <regex>
#include <vector>

#include <string>

namespace sats {

// struct SentinelL2AProductLoadError {
//   bool error = false;
//   std::string message;
// };
//
// struct SentinelL2ATileBand {
//   std::string bandName;
//   std::string bandImage;
//   int bandImageResolution;
//
//   std::string qiBandImage;
//   int qiBandImageResolution;
// };
//
// struct SentinelL2ATile {
//   std::map<std::string, SentinelL2ATileBand> bands;
//
//   std::string cloudProbabilityImage;
//   int cloudProbabilityImageResolution;
//
//   std::string snowIceProbabilityImage;
//   int snowIceProbabilityImageResolution;
//
//   std::string tileID;
// };
//
// struct SentinelL2AProduct {
//   std::vector<SentinelL2ATile> tiles;
// };
//
// SentinelL2AProductLoadError loadProductTile(const std::string &tileRoot,
//                                             const std::string &tileName,
//                                             SentinelL2ATile *oTile) {
//   oTile->tileID = tileName;
//
//   std::string workingPath = tileRoot + "/IMG_DATA/";
//
//   {
//     std::regex re(R"(R[0-9]+m/)" + tileName +
//                   R"(_[0-9A-Z]+_(B[0-9A-Z]+)_([0-9]+)m.jp2)");
//     char **entries = VSIReadDirRecursive(workingPath.c_str());
//
//     if (entries == nullptr) {
//       return SentinelL2AProductLoadError{
//           .error = true, .message = "VSI could not read " + workingPath};
//     }
//
//     char **it = entries;
//     while (*it != nullptr) {
//       std::string entry = *it;
//       std::smatch match;
//       if (!std::regex_search(entry, match, re)) {
//         it++;
//         continue;
//       }
//
//       std::string bandName = entry.substr(match.position(1),
//       match.length(1)); int resolution =
//           std::stoi(entry.substr(match.position(2), match.length(2)));
//
//       if (oTile->bands.contains(bandName)) {
//         if (oTile->bands[bandName].bandImageResolution > resolution) {
//           oTile->bands[bandName].bandImage = workingPath + "/" + entry;
//           oTile->bands[bandName].bandImageResolution = resolution;
//         }
//       } else {
//         oTile->bands[bandName] = SentinelL2ATileBand();
//         oTile->bands[bandName].bandName = bandName;
//         oTile->bands[bandName].bandImage = workingPath + "/" + entry;
//         oTile->bands[bandName].bandImageResolution = resolution;
//       }
//
//       it++;
//     }
//
//     workingPath = tileRoot + "/QI_DATA/";
//     {
//       std::regex re(R"(R[0-9]+m/)" + tileName +
//                     R"(_[0-9A-Z]+_(B[0-9A-Z]+)_([0-9]+)m.jp2)");
//       char **entries = VSIReadDirRecursive(workingPath.c_str());
//
//       if (entries == nullptr) {
//         return SentinelL2AProductLoadError{
//             .error = true, .message = "VSI could not read " + workingPath};
//       }
//
//       char **it = entries;
//       while (*it != nullptr) {
//         std::string entry = *it;
//         std::smatch match;
//         if (!std::regex_search(entry, match, re)) {
//           it++;
//           continue;
//         }
//
//         std::string bandName = entry.substr(match.position(1),
//         match.length(1)); int resolution =
//             std::stoi(entry.substr(match.position(2), match.length(2)));
//
//         if (oTile->bands.contains(bandName)) {
//           if (oTile->bands[bandName].bandImageResolution > resolution) {
//             oTile->bands[bandName].bandImage = workingPath + "/" + entry;
//             oTile->bands[bandName].bandImageResolution = resolution;
//           }
//         } else {
//           oTile->bands[bandName] = SentinelL2ATileBand();
//           oTile->bands[bandName].bandName = bandName;
//           oTile->bands[bandName].bandImage = workingPath + "/" + entry;
//           oTile->bands[bandName].bandImageResolution = resolution;
//         }
//
//         it++;
//       }
//
//       CPLFree(entries);
//     }
//
//     return SentinelL2AProductLoadError();
//   }
//
//   SentinelL2AProductLoadError loadSentinelProductZip(
//       const std::filesystem::path path, SentinelL2AProduct *oProduct) {
//
//     std::string workingPath = "/vsizip/" + path.string();
//     {
//       char **entries = VSIReadDir(workingPath.c_str());
//
//       if (entries == nullptr) {
//         return SentinelL2AProductLoadError{
//             .error = true, .message = "VSI could not read zip!"};
//       }
//
//       workingPath += "/" + std::string((const char *)*entries);
//
//       std::cout << "working path: " << workingPath << std::endl;
//
//       CPLFree(entries);
//     }
//
//     workingPath += "/GRANULE";
//     {
//       char **entries = VSIReadDir(workingPath.c_str());
//
//       if (entries == nullptr) {
//         return SentinelL2AProductLoadError{
//             .error = true,
//             .message = "VSI could not read GRANULE subdir of product!"};
//       }
//
//       std::regex re(R"(L2A_([A-Z][1-9]{2}[A-Z]+)_A[0-9]+_[A-Z0-9]+)");
//       char **it = entries;
//       while (*it != nullptr) {
//         std::string folderName(*it);
//
//         std::smatch match;
//         if (!std::regex_search(folderName, match, re)) {
//           it++;
//           continue;
//         }
//
//         std::string tileName =
//             folderName.substr(match.position(1), match.length(1));
//
//         std::cout << "tile name: " << tileName << std::endl;
//         SentinelL2ATile tile{};
//         loadProductTile(workingPath + "/" + folderName, tileName, &tile);
//
//         it++;
//       }
//
//       CPLFree(entries);
//     }
//
//     return SentinelL2AProductLoadError();
//   }

void loadSentinelProductZips(const std::vector<std::filesystem::path> &paths,
                             const std::vector<std::string> &bands,
                             const std::vector<std::string> &qaBands) {
  for (const auto &path : paths) {
    if (!std::filesystem::exists(path)) {
      std::cerr << "file " << path << " does not exist" << std::endl;
      return;
    }

    if (!std::filesystem::is_regular_file(path) ||
        !path.filename().string().ends_with(".zip")) {
      std::cerr << "file " << path << " is not a zip file" << std::endl;
      return;
    }

    GDALAllRegister();
    GDALDatasetUniquePtr dsPtr = GDALDatasetUniquePtr(
        GDALDataset::FromHandle(GDALOpen(path.c_str(), GA_ReadOnly)));

    if (!dsPtr) {
      std::cout << "failed to load dataset " << path << std::endl;
      return;
    }

    double adfGeoTransform[6];
    printf("Driver: %s/%s\n", dsPtr->GetDriver()->GetDescription(),
           dsPtr->GetDriver()->GetMetadataItem(GDAL_DMD_LONGNAME));
    printf("Size is %dx%dx%d\n", dsPtr->GetRasterXSize(),
           dsPtr->GetRasterYSize(), dsPtr->GetRasterCount());
    if (dsPtr->GetProjectionRef() != NULL)
      printf("Projection is `%s'\n", dsPtr->GetProjectionRef());
    if (dsPtr->GetGeoTransform(adfGeoTransform) == CE_None) {
      printf("Origin = (%.6f,%.6f)\n", adfGeoTransform[0], adfGeoTransform[3]);
      printf("Pixel Size = (%.6f,%.6f)\n", adfGeoTransform[1],
             adfGeoTransform[5]);
    }

    char **entries = dsPtr->GetMetadata("SUBDATASETS");

    {
      if (!entries) {
        std::cout << "could not load subdatasets" << std::endl;
        return;
      }

      char **it = entries;

      while (*it) {
        std::cout << *it << std::endl;
        it++;
      }
    }

    // auto error = loadSentinelProductZip(path, nullptr);

    // if (error.error) {
    //   std::cout << "sentinel load error: " << error.message << std::endl;
    // }

    // std::string pth = "/vsizip/" + (path).string() +
    //                   "/S2B_MSIL1C_20220509T170849_N0400_R112_T14TQL_"
    //                   "20220509T202750.SAFE/GRANULE/IMG_DATA/";
    // char **entries = VSIReadDirRecursive((pth).c_str());
    //
    // std::map<std::string, std::string> bandToPathMap;
    //
    // if (entries != nullptr) {
    //   char **entry = entries;
    //
    //   while (*entry != nullptr) {
    //     std::string s = std::string((const char *)*entry);
    //
    //     for (const auto &band : bands) {
    //       std::cout << "entry: " << s << std::endl;
    //       if (s.ends_with(band + ".jp2")) {
    //         bandToPathMap[band] = s;
    //         break;
    //       }
    //     }
    //
    //     entry++;
    //   }
    //
    //   CSLDestroy(entries);
    // } else {
    //   std::cerr << "could not read contents of zip file: " << path <<
    //   std::endl; return;
    // }
    //
    // bool missingBands = false;
    // for (const auto &band : bands) {
    //   if (!bandToPathMap.contains(band)) {
    //     missingBands = true;
    //     std::cerr << "band \"" << band << "\" not found in product zip!"
    //               << std::endl;
    //   }
    // }
    //
    // if (missingBands) {
    //   return;
    // }
    //
    // for (const auto &[band, path] : bandToPathMap) {
    //   std::cout << "loading path: " << path << std::endl;
    // }
  }
}
} // namespace sats
