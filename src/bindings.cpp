#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/ndarray.h>

#include "generation_tasks.hpp"

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(_core, m)
{
    m.doc() = "SingleLineTextGenerator C++ core module";

    nb::class_<SingleLineTextGenerator>(m, "SingleLineTextGenerator")
        .def(nb::init<const std::string &>(), "config_str"_a,
             "Construct from a YAML or JSON configuration string.")
        .def("generate", &SingleLineTextGenerator::generate,
             "total"_a, "workers"_a = 0, "show_progress"_a = true,
             "Generate a batch of OCR training images. Blocks until complete. "
             "Returns (total_generated, total_errors).")
        .def("generate_instance_file", &SingleLineTextGenerator::generateInstanceFile,
             "text"_a, "save_path"_a,
             "Generate a single OCR image and save to file.")
        .def("generate_instance_explicit", [](SingleLineTextGenerator &self, const std::string &text) {
            auto result = self.generateInstanceExplicit(text);
            size_t shape[3] = {(size_t)result.height, (size_t)result.width, 3};
            auto *data = new std::vector<uint8_t>(std::move(result.data));
            nb::capsule owner(data, [](void *p) noexcept {
                delete static_cast<std::vector<uint8_t> *>(p);
            });
            return nb::ndarray<nb::numpy, uint8_t, nb::shape<-1, -1, 3>>(
                data->data(), 3, shape, owner);
        }, "text"_a,
           "Generate a single OCR image and return as numpy array (H, W, 3) in RGB format.")
        .def("get_config_json", &SingleLineTextGenerator::getConfigJson,
             "Get the active configuration as a JSON string.");
}