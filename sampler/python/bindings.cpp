#include <ATen/core/ATen_fwd.h>
#include <ATen/ops/tensor.h>
#include <ATen/ops/zero.h>
#include <c10/core/TensorOptions.h>
#include <filesystem>

#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <torch/extension.h>

#include "cdlCache.h"
#include "sampler.h"

namespace py = pybind11;

std::pair<torch::Tensor, torch::Tensor> randomBatch(sats::Sampler &m,
                                                    size_t n) {
  std::vector<sats::Sampler::Sample> samples = m.randomSampleV2(n);

  auto tensorOpts = at::TensorOptions()
                        .device("cpu")
                        .dtype(torch::kFloat32)
                        .memory_format(torch::MemoryFormat::Contiguous);
  torch::Tensor tensor =
      torch::empty({static_cast<long>(n), (long)samples[0].bands.size(),
                    (long)m.getSampleDim(), (long)m.getSampleDim()},
                   tensorOpts);

#pragma omp parallel for
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < samples[0].bands.size(); j++) {
      using namespace torch::indexing;
      torch::Tensor t = tensor[i][j];

      memcpy((float *)t.data_ptr(), samples[i].bands[j].data(),
             m.getSampleDim() * m.getSampleDim() * sizeof(float));
    }
  }

  torch::Tensor cdlTensor = torch::empty(
      {static_cast<long>(n), 1, (long)m.getSampleDim(), (long)m.getSampleDim()},
      tensorOpts);

#pragma omp parallel for
  for (size_t i = 0; i < n; i++) {
    std::string cdlPath = m.yearToCDL[samples[i].year];

    if (cdlPath.empty()) {
      std::cout << "cdl for year " << samples[i].year << " is empty!"
                << std::endl;
      continue;
    }

    torch::Tensor t = cdlTensor[i][0];

    std::vector<float> dat =
        sats::cdl::read(cdlPath, samples[i].crs.c_str(),
                        sats::cdl::ProjWin{
                            .xmin = (double)samples[i].coordsMin.first,
                            .xmax = (double)samples[i].coordsMax.first,
                            .ymin = (double)samples[i].coordsMin.second,
                            .ymax = (double)samples[i].coordsMax.second,
                        },
                        (double)m.getSampleDim(), (double)m.getSampleDim());

    if (dat.empty()) {
      std::cout << "cdl read for " << samples[i].year << " returned empty!"
                << std::endl;
      continue;
    }

    memcpy((float *)t.data_ptr(), dat.data(),
           m.getSampleDim() * m.getSampleDim() * sizeof(float));
  }

  return std::make_pair(tensor, cdlTensor);
}

PYBIND11_MODULE(satsamplepy, m) {
  py::class_<sats::Sampler::SampleOptions>(m, "SampleOptions")
      .def(py::init<>())
      .def_readwrite("dbPath", &sats::Sampler::SampleOptions::dbPath)
      .def_readwrite("nCacheGenThreads",
                     &sats::Sampler::SampleOptions::nCacheGenThreads)
      .def_readwrite("nCacheQueryThreads",
                     &sats::Sampler::SampleOptions::nCacheQueryThreads)
      .def(py::pickle(
          [](const sats::Sampler::SampleOptions &s) {
            return py::make_tuple(s.dbPath, s.nCacheGenThreads,
                                  s.nCacheQueryThreads);
          },
          [](py::tuple t) {
            return sats::Sampler::SampleOptions{
                t[0].cast<std::filesystem::path>(),
                t[1].cast<size_t>(),
                t[2].cast<size_t>(),
            };
          }));
  py::class_<sats::Sampler::SampleCacheGenOptions>(m, "SampleCacheGenOptions")
      .def(py::init<>())
      .def_readwrite("minOKPercentage",
                     &sats::Sampler::SampleCacheGenOptions::minOKPercentage)
      .def_readwrite("sampleDim",
                     &sats::Sampler::SampleCacheGenOptions::sampleDim)
      .def_readwrite("cldMax", &sats::Sampler::SampleCacheGenOptions::cldMax)
      .def_readwrite("snwMax", &sats::Sampler::SampleCacheGenOptions::snwMax)
      .def(py::pickle(
          [](const sats::Sampler::SampleCacheGenOptions &s) {
            return py::make_tuple(s.minOKPercentage, s.sampleDim, s.cldMax,
                                  s.snwMax);
          },
          [](py::tuple t) {
            return sats::Sampler::SampleCacheGenOptions{
                t[0].cast<float>(),
                t[1].cast<size_t>(),
                t[2].cast<uint8_t>(),
                t[3].cast<uint8_t>(),
            };
          }));
  py::class_<sats::DateRange>(m, "DateRange")
      .def(py::init<>())
      .def_readwrite("minYear", &sats::DateRange::minYear)
      .def_readwrite("minMonth", &sats::DateRange::minMonth)
      .def_readwrite("minDay", &sats::DateRange::minDay)
      .def_readwrite("maxYear", &sats::DateRange::maxYear)
      .def_readwrite("maxMonth", &sats::DateRange::maxMonth)
      .def_readwrite("maxDay", &sats::DateRange::maxDay)
      .def(py::pickle(
          [](const sats::DateRange &s) {
            return py::make_tuple(s.minYear, s.minMonth, s.minDay, s.maxYear,
                                  s.maxMonth, s.maxDay);
          },
          [](py::tuple t) {
            return sats::DateRange{
                t[0].cast<size_t>(), t[1].cast<size_t>(), t[2].cast<size_t>(),
                t[3].cast<size_t>(), t[4].cast<size_t>(), t[5].cast<size_t>(),
            };
          }));

  py::class_<sats::Sampler::Sample>(m, "Sample")
      .def(py::init<>())
      .def_readwrite("bands", &sats::Sampler::Sample::bands)
      .def_readwrite("coords_min", &sats::Sampler::Sample::coordsMin)
      .def_readwrite("coords_max", &sats::Sampler::Sample::coordsMax)
      .def_readwrite("crs", &sats::Sampler::Sample::crs)
      .def_readwrite("year", &sats::Sampler::Sample::year)
      .def_readwrite("month", &sats::Sampler::Sample::month)
      .def_readwrite("day", &sats::Sampler::Sample::day)
      .def(py::pickle(
          [](const sats::Sampler::Sample &s) {
            return py::make_tuple(s.bands, s.coordsMin, s.coordsMax, s.crs,
                                  s.year, s.month, s.day);
          },
          [](py::tuple t) {
            return sats::Sampler::Sample{
                t[0].cast<std::vector<std::vector<float>>>(),
                t[1].cast<std::string>(),
                t[2].cast<std::pair<size_t, size_t>>(),
                t[3].cast<std::pair<size_t, size_t>>(),
                t[4].cast<size_t>(),
                t[5].cast<size_t>(),
                t[6].cast<size_t>(),
            };
          }));

  py::class_<sats::Sampler>(m, "Sampler")
      .def(py::init<const std::filesystem::path &, sats::Sampler::SampleOptions,
                    sats::Sampler::SampleCacheGenOptions,
                    std::optional<sats::DateRange>, bool>(),
           py::arg("path"), py::arg("sample_options"), py::arg("cache_options"),
           py::arg("date_range"), py::arg("should_preproc") = false)
      .def("randomSample", &sats::Sampler::randomSampleV2, "get random samples",
           py::arg("n"))
      .def("randomSample2", &randomBatch, "get random samples", py::arg("n"))
      .def(py::pickle(
          [](const sats::Sampler &s) {
            return py::make_tuple(s.dataPath, s.sampleOptions,
                                  s.cacheGenOptions, s.dateRange, s.preproc);
          },
          [](py::tuple t) {
            return sats::Sampler(
                t[0].cast<std::filesystem::path>(),
                t[1].cast<sats::Sampler::SampleOptions>(),
                t[2].cast<sats::Sampler::SampleCacheGenOptions>(),
                t[3].cast<std::optional<sats::DateRange>>(), t[4].cast<bool>());
          }));
  m.attr("__version__") = "dev";
  // py::implicitly_convertible<std::string, std::filesystem::path>();
}
