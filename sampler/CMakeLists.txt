cmake_minimum_required(VERSION 3.24)

project(sat-sample)

find_package(SQLite3)
find_package(GDAL CONFIG)
find_package(PkgConfig REQUIRED)
if(NOT GDAL_FOUND)
    find_package(GDAL REQUIRED)
endif()

find_package(CUDAToolkit)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_library(satsample SHARED src/sampler.cpp src/cpu/mapgen.cpp src/cpu/percentile.cpp src/cdlCache.cpp)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    target_compile_options(satsample PRIVATE "-D_GLIBCXX_USE_CXX11_ABI=0")
endif()

if (CUDAToolkit_FOUND)
    enable_language(CUDA)
    # set(CMAKE_CUDA_FLAGS ${CMAKE_CUDA_FLAGS} "-g -G")
    # set(CUDA_NVCC_FLAGS "-g -G")
    target_sources(satsample PRIVATE src/cuda/mapgen.cu src/cuda/maskJoin.cu src/cuda/percentile.cu)
    target_compile_definitions(satsample PRIVATE HAS_CUDA)
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -default-stream per-thread")
    target_link_libraries(satsample PUBLIC CUDA::cudart)
endif()

find_package(OpenMP REQUIRED)
find_package(OpenCV REQUIRED)

target_compile_features(satsample PUBLIC cxx_std_20)

include(FetchContent)

target_link_libraries(satsample PUBLIC SQLite::SQLite3 GDAL::GDAL OpenMP::OpenMP_CXX ${OpenCV_LIBS})
target_include_directories(satsample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${OpenCV_INCLUDE_DIRS})

# FetchContent_Declare(
#     googletest
#     URL https://github.com/google/googletest/archive/2b6b042a77446ff322cd7522ca068d9f2a21c1d1.zip
# )
# FetchContent_MakeAvailable(googletest)
#
# enable_testing()
#
# add_executable(sampler_test test/sampler.cpp)
# target_link_libraries(sampler_test PUBLIC GTest::gtest_main satsample OpenMP::OpenMP_CXX)
#
# add_executable(sandbox sandbox/main.cpp)
#
# target_link_libraries(sandbox PRIVATE satsample)

# include(GoogleTest)
# gtest_discover_tests(sampler_test)

# install(TARGETS satsample DESTINATION satsample)

# Python bindings
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11
    GIT_TAG v2.13.6
)
FetchContent_MakeAvailable(pybind11
    # BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/pybind11
)
set(pybind11_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/pybind11)

add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

set(BUILD_SHARED_LIBS OFF)
find_package(Torch REQUIRED)
find_library(TORCH_PYTHON_LIBRARY torch_python PATHS "${TORCH_INSTALL_PREFIX}/lib")
find_library(TORCH_LIBRARY torch PATHS "${TORCH_INSTALL_PREFIX}/lib")
find_library(TORCH_C_LIBRARY c10 PATHS "${TORCH_INSTALL_PREFIX}/lib")

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)

message("TORCH LIBRARIES: ${TORCH_LIBRARIES}")
message("TORCH PYTHON LIBRARY: ${TORCH_PYTHON_LIBRARY}")

pybind11_add_module(satsamplepy MODULE python/bindings.cpp)
target_link_libraries(satsamplepy PUBLIC ${TORCH_LIBRARIES} ${TORCH_PYTHON_LIBRARY})
target_link_libraries(satsamplepy PUBLIC pybind11::headers)
target_link_libraries(satsamplepy PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,satsample>")
target_include_directories(satsamplepy PUBLIC ${TORCH_INCLUDE_DIRS})
target_compile_features(satsamplepy PUBLIC cxx_std_20)
message("TORCH INCLUDES: ${TORCH_INCLUDE_DIRS}")
target_compile_features(satsamplepy PUBLIC cxx_std_20)

install(TARGETS satsamplepy DESTINATION satsamplepy)
install(TARGETS satsample DESTINATION satsamplepy/lib)
# install(IMPORTED_RUNTIME_ARTIFACTS satsample DESTINATION satsamplepy)

message("output: ${SKBUILD_PLATLIB_DIR}")
