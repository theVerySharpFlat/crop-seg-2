#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/mat.hpp>
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
          .minOKPercentage = 0.9999,
          .sampleDim = 256,
          .cldMax = 50,
          .snwMax = 50,
      },
      std::nullopt);

  for (size_t i = 0; i < 100; i++) {
    size_t nSamples = 1;
    auto samples = s.randomSampleV2(nSamples);
    assert(samples.size() == nSamples);

    for (const auto &sample : samples) {
      cv::Mat temp;
      cv::Mat r, g, b;

      r = cv::Mat(256, 256, CV_32F, (float *)sample.bands[0].data());

      g = cv::Mat(256, 256, CV_32F, (float *)sample.bands[1].data());

      b = cv::Mat(256, 256, CV_32F, (float *)sample.bands[2].data());

      // cv::Mat c =
      //     cv::Mat(256, 256, CV_32FC1,
      //             (float *)sample.bands[sample.bands.size() - 2].data());
      // c /= 11.0;

      cv::Mat ndvi =
          cv::Mat(256, 256, CV_32FC1,
                  (float *)sample.bands[sample.bands.size() - 1].data());
      for (int r = 0; r < ndvi.rows; r++) {
        for (int c = 0; c < ndvi.cols; c++) {
          if (ndvi.at<float>(cv::Vec2i{r, c}) < 0.2) {
            ndvi.at<float>(cv::Vec2i{r, c}) = 0.0;
          }
        }
      }
      cv::Mat ndviOut;
      cv::merge(std::vector{cv::Mat(256, 256, CV_32FC1, 0.0), ndvi,
                            cv::Mat(256, 256, CV_32FC1, 0.0)},
                ndviOut);

      cv::Mat together;
      cv::merge(std::vector{b, g, r}, together);
      together.convertTo(together, CV_32FC3);

      // cv::imshow("bro???", c);
      cv::imshow("bro?", together);
      cv::imshow("bro????", ndviOut);
      cv::waitKey();
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
