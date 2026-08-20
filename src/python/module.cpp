#include <pybind11/pybind11.h>
#include "bindings.hpp"

PYBIND11_MODULE(molshredder, module) {
  molshredder::python::populate_module(module);
}
