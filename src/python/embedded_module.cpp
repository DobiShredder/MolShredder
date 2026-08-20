#include <pybind11/embed.h>

#include "bindings.hpp"
#include "embedded_module.hpp"

PYBIND11_EMBEDDED_MODULE(molshredder, module) {
  molshredder::python::populate_module(module);
}

namespace molshredder::python {

void link_embedded_module() noexcept {}

}  // namespace molshredder::python
