#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/ndarray.h>
#include "neo_synthesizer.hpp"

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(_core, m)
{
    m.doc() = "NeoTextSynthesizer C++ core module";

    nb::class_<NeoTextSynthesizer>(m, "NeoTextSynthesizer")
        // Construct from a YAML/JSON config string
        .def(nb::init<const std::string &>(), "config_str"_a,
             "Construct from a YAML or JSON configuration string.")

        // Batch generate
        .def("generate", &NeoTextSynthesizer::generate,
             "total"_a, "workers"_a = 0, "show_progress"_a = true,
             "Generate a batch of OCR training images. Blocks until complete.")

        // Single instance: save to file
        .def("generate_instance_file", &NeoTextSynthesizer::generateInstanceFile,
             "text"_a, "save_path"_a,
             "Generate a single OCR image and save to file.")

        // Single instance: return as numpy array
        .def("generate_instance_explicit", [](NeoTextSynthesizer &self, const std::string &text) {
            auto result = self.generateInstanceExplicit(text);
            // Create a numpy array with shape (H, W, 3)
            size_t shape[3] = {(size_t)result.height, (size_t)result.width, 3};
            // We need to transfer ownership of the data to Python
            auto *data = new std::vector<uint8_t>(std::move(result.data));
            nb::capsule owner(data, [](void *p) noexcept {
                delete static_cast<std::vector<uint8_t> *>(p);
            });
            return nb::ndarray<nb::numpy, uint8_t, nb::shape<-1, -1, 3>>(
                data->data(), 3, shape, owner);
        }, "text"_a,
           "Generate a single OCR image and return as numpy array (H, W, 3) in RGB format.")

        // Get config as JSON string
        .def("get_config_json", [](const NeoTextSynthesizer &self) {
            return self.getConfig().dump();
        }, "Get the active configuration as a JSON string.");
}