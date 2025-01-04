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
  sats::Sampler s("../data/2022/10",
                  {
                      .minOKPercentage = 0.99,
                      .sampleDim = 256,
                      .cldMax = 50,
                      .snwMax = 50,
                  },
                  std::nullopt);

  std::vector<float *> bands = s.randomSample();

  cv::Mat temp;
  cv::Mat r, g, b;

  temp = cv::Mat(256, 256, CV_32F, bands[0]);

  cv::divide(40000.0, temp, r);

  temp = cv::Mat(256, 256, CV_32F, bands[1]);
  cv::divide(40000.0, temp, g);

  temp = cv::Mat(256, 256, CV_32F, bands[2]);
  cv::divide(40000.0, temp, b);

  cv::Mat together;
  cv::merge(std::vector{b, g, r}, together);

  cv::Mat togetherOut;
  together.convertTo(togetherOut, CV_8UC3);

  cv::imwrite("out.png", togetherOut);
  // cv::imshow("bro?", normalized);
  // cv::waitKey();
}
