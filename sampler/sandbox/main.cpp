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
      {.dbPath = "cache.db", .nCacheGenThreads = 8, .nCacheQueryThreads = 16},
      {
          .minOKPercentage = 0.99,
          .sampleDim = 256,
          .cldMax = 50,
          .snwMax = 50,
      },
      std::nullopt);

  // while (true) {
  //   std::vector<float *> bands = s.randomSample();
  //
  //   cv::Mat temp;
  //   cv::Mat r, g, b;
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[0]);
  //
  //   cv::divide(100000.0, temp, r);
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[1]);
  //   cv::divide(100000.0, temp, g);
  //
  //   temp = cv::Mat(256, 256, CV_32F, bands[2]);
  //   cv::divide(100000.0, temp, b);
  //
  //   cv::Mat together;
  //   cv::merge(std::vector{b, g, r}, together);
  //   together.convertTo(together, CV_8UC3);
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
  //   cv::imshow("bro?", together);
  //   // cv::waitKey();
  //   //
  //   for (const auto &band : bands) {
  //     free(band);
  //   }
  // }
}
