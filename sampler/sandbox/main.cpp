#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/matx.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <optional>
#include <sampler.h>

#include <opencv2/opencv.hpp>

int main() {
  std::srand(time(NULL));

  sats::Sampler s(
      "../data/",
      {.dbPath = "cache.db", .nCacheGenThreads = 16, .nCacheQueryThreads = 32},
      {
          .minOKPercentage = 0.99,
          .sampleDim = 256,
          .cldMax = 50,
          .snwMax = 50,
      },
      std::nullopt);

  for (size_t i = 0; i < 100; i++) {
    size_t nSamples = 100;
    auto samples = s.randomSampleV2(nSamples);
    assert(samples.size() == nSamples);

    for (const auto &sample : samples) {
      cv::Mat temp;
      cv::Mat r, g, b;

      r = cv::Mat(256, 256, CV_32F, sample[0]);

      g = cv::Mat(256, 256, CV_32F, sample[1]);

      b = cv::Mat(256, 256, CV_32F, sample[2]);

      cv::Mat together;
      cv::merge(std::vector{b, g, r}, together);
      together.convertTo(together, CV_32FC3);

      cv::imshow("bro?", together);
      cv::waitKey();

      for (const auto &band : sample) {
        free(band);
      }
    }
  }

  // for (const std::vector<float *> &bands : samples) {
  //   cv::Mat temp;
  //   cv::Mat r, g, b;
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[0]);
  //
  //   cv::divide(150000.0, temp, r);
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[1]);
  //   cv::divide(150000.0, temp, g);
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[2]);
  //   cv::divide(150000.0, temp, b);
  //
  //   cv::Mat together;
  //   cv::merge(std::vector{b, g, r}, together);
  //   together.convertTo(together, CV_8UC3);
  //
  //   cv::Mat clouds = cv::Mat(256, 256, CV_32F, bands[bands.size() - 2]);
  //   cv::divide(10.0, clouds, clouds);
  //
  //   // cv::Mat kmeans;
  //   // cv::kmeans(together, 10, kmeans,
  //   //            cv::TermCriteria(cv::TermCriteria::COUNT, 10, 1.), 10,
  //   //            cv::KMEANS_PP_CENTERS);
  //   // // auto newSize = cv::Size(together.cols, together.rows);
  //   // kmeans = kmeans.reshape(1, together.rows);
  //   // kmeans.convertTo(kmeans, CV_32F);
  //   // cv::normalize(kmeans, kmeans);
  //
  //   // cv::Mat togetherOut;
  //   // kmeans.convertTo(togetherOut, CV_8U);
  //
  //   // cv::imwrite("out.png", togetherOut);
  //   // cv::namedWindow("bro?");
  //   cv::imshow("bro?", together);
  //   cv::imshow("clouds?", clouds);
  //   cv::waitKey();
  //   //
  //   for (const auto &band : bands) {
  //     free(band);
  //   }
  // }
}
