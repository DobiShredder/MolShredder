#pragma once

#include <pybind11/pybind11.h>

namespace molshredder::python {

void populate_module(pybind11::module_& module);

}  // namespace molshredder::python
