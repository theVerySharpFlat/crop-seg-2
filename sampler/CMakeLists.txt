cmake_minimum_required(VERSION 3.16)

project(sat-sample)

find_package(GDAL CONFIG)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

add_library(satsample src/sampler.cpp src/cuda/mapgen.cu src/cuda/maskJoin.cu)
target_compile_features(satsample PUBLIC cxx_std_20)
target_link_libraries(satsample PUBLIC GDAL::GDAL CUDA::cudart)
target_include_directories(satsample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(sandbox sandbox/main.cpp)
target_link_libraries(sandbox PRIVATE satsample)
