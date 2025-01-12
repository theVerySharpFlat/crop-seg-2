cmake_minimum_required(VERSION 3.16)

project(sat-sample)

find_package(GDAL CONFIG)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

find_package(CUDAToolkit)

add_library(satsample src/sampler.cpp src/cpu/mapgen.cpp src/cdlCache.cpp)

if (CUDAToolkit_FOUND)
    enable_language(CUDA)
    # set(CMAKE_CUDA_FLAGS ${CMAKE_CUDA_FLAGS} "-g -G")
    # set(CUDA_NVCC_FLAGS "-g -G")
    target_sources(satsample PRIVATE src/cuda/mapgen.cu src/cuda/maskJoin.cu)
    target_compile_definitions(satsample PRIVATE HAS_CUDA)
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -default-stream per-thread")
    target_link_libraries(satsample PUBLIC CUDA::cudart)
endif()

find_package(OpenMP REQUIRED)
find_package(OpenCV REQUIRED)

target_compile_features(satsample PUBLIC cxx_std_20)
target_link_libraries(satsample PUBLIC GDAL::GDAL OpenMP::OpenMP_CXX ${OpenCV_LIBS})
target_include_directories(satsample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})

add_executable(sandbox sandbox/main.cpp)
target_link_libraries(sandbox PRIVATE satsample)
