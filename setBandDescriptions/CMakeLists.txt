cmake_minimum_required(VERSION 3.16)

project(sat-sample LANGUAGES CXX)

find_package(GDAL CONFIG)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

add_executable(satsample_setbandinfo main.cpp)
target_link_libraries(satsample_setbandinfo PUBLIC GDAL::GDAL)
target_compile_features(satsample_setbandinfo PUBLIC cxx_std_20)

# target_compile_options(satsample_mapgen PUBLIC -fsanitize=address)
# target_link_options(satsample_mapgen PUBLIC -fsanitize=address)
