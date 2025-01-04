#include <iostream>

#include <optional>
#include <sampler.h>

int main() {
  sats::Sampler s("../data/2022",
                  {
                      .minOKPercentage = 0.99,
                      .sampleDim = 256,
                      .cldMax = 50,
                      .snwMax = 50,
                  },
                  std::nullopt);
  s.randomSample();
}
