#include <iostream>

#include <load.h>

int main() {
  std::cout << "hello, world" << std::endl;
  sats::loadSentinelProductZips(
      {"/home/aiden/projects/crop-seg-2/sampler/data/2022/5/14TQL/"
       "S2B_MSIL2A_20220509T170849_N0400_R112_T14TQL_20220509T212451.SAFE.zip",
       "/home/aiden/projects/crop-seg-2/sampler/data/2022/5/14TQM/"
       "S2B_MSIL2A_20220509T170849_N0400_R112_T14TQM_20220509T212451.SAFE.zip"},
      {"B01", "B03", "B05"}, {});
}
