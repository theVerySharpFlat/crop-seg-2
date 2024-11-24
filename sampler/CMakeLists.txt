cmake_minimum_required(VERSION 3.16)

project(sat-sample)

find_package(GDAL CONFIG)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

add_library(satsample src/load.cpp)
target_compile_features(satsample PUBLIC cxx_std_20)
target_link_libraries(satsample PUBLIC GDAL::GDAL)
target_include_directories(satsample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(sandbox sandbox/main.cpp)
target_link_libraries(sandbox PRIVATE satsample)
