cmake_minimum_required(VERSION 3.16)

project(sat-sample LANGUAGES CUDA CXX)

find_package(GDAL CONFIG)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

enable_language(CUDA)
# check_language(CUDA)

find_package(CUDAToolkit REQUIRED)

add_executable(satsample_mapgen main.cpp cuda/mapgen.cu cuda/maskJoin.cu)
target_link_libraries(satsample_mapgen PUBLIC GDAL::GDAL CUDA::cudart)
target_compile_features(satsample_mapgen PUBLIC cxx_std_20)

# target_compile_options(satsample_mapgen PUBLIC -fsanitize=address)
# target_link_options(satsample_mapgen PUBLIC -fsanitize=address)
