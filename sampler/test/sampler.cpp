#include <csignal>
#include <format>
#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/matx.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <optional>

#include <opencv2/opencv.hpp>

#include <omp.h>

#define TESTING
#include "sampler.h"

TEST(SamplerTest, IndexTest) {
  std::srand(time(NULL));

  sats::Sampler s("../data/2022/10",
                  {
                    .dbPath = "cache-test.db",
                    .nCacheGenThreads = 1,
                    .nCacheQueryThreads = 1,
                  },
                  {
                      .minOKPercentage = 0.99,
                      .sampleDim = 256,
                      .cldMax = 50,
                      .snwMax = 50,
                  },
                  std::nullopt);

  // for (const auto &info : s.infos) {
  // uint64_t lower =
  //     ((uint64_t *)info.cache->bitrange)[info.cache->nCols / 64 - 2];
  // uint64_t middle =
  //     ((uint64_t *)info.cache->bitrange)[info.cache->nCols / 64 - 1];
  // uint64_t upper =
  //     ((uint64_t *)info.cache->bitrange)[info.cache->nCols / 64 - 0];
  // std::cout << "lower, upper: "
  //           << std::format("{:064b},{:064b},{:064b}", lower, middle, upper)
  //           << std::endl;
  //

  // ASSERT_EQ(false, true);
  const auto &info = s.infos[0];
  ASSERT_EQ(info.cache->size % 8, 0);
  size_t scalingFactor = info.maxDimX / info.cache->nCols;

  omp_set_dynamic(1);
  omp_set_num_threads(32);
#pragma omp parallel
  {
#pragma omp single
    for (size_t i = 0; i < info.cache->nOK; i++) {
#pragma omp task
      {
        size_t sampleIndex = s.computeSampleIndex(i, info.cache.value());

        size_t sampleCol = sampleIndex % info.cache->nCols;
        size_t sampleRow = sampleIndex / info.cache->nCols;

        // std::cout << "sample index: " << sampleIndex << std::endl;
        // std::cout << "sampleRow: " << std::format("{}, {}", sampleCol,
        // sampleRow)
        //           << std::endl;
        //
        // for (size_t j = info.cache->nCols - 8 * 2; j < info.cache->nCols + 8;
        //      j++) {
        //   std::cout << std::format("{:08b}", info.cache->bitrange[j]);
        // }
        // std::cout << std::endl;

        // if (i > 1024) {
        //   for (size_t j = sampleIndex / 8 - 1 * 8; j < sampleIndex / 8 + 8;
        //   j++) {
        //     std::cout << std::format(" {:08b} ", info.cache->bitrange[j]);
        //   }
        //   std::cout << std::endl;
        //
        //   for (size_t j = (sampleIndex / 8 - 1 * 8) * 8;
        //        j < (sampleIndex / 8 + 8) * 8; j += 8) {
        //     std::cout << std::format(" {:08} ", j);
        //   }
        //   std::cout << std::endl;
        // }

        EXPECT_LE(sampleCol + s.cacheGenOptions.sampleDim / scalingFactor,
                  info.cache->nCols);
        EXPECT_LE(sampleRow + s.cacheGenOptions.sampleDim / scalingFactor,
                  info.cache->nRows);

        // if (!(i % 100000)) {
        //   std::cout << i << "/" << info.cache->nOK << std::endl;
        // }

        // std::cout << std::endl;
      }
    }
  }
  // }
}
