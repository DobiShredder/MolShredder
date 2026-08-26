cmake_minimum_required(VERSION 3.25)

project(
  MolShredder
  VERSION 0.1.0
  DESCRIPTION "High-performance molecular visualization and analysis"
  LANGUAGES C CXX
)

# Prevent the Windows SDK min/max macros from corrupting standard-library
# calls such as std::numeric_limits<T>::max() in transitive Windows includes.
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

find_package(Threads REQUIRED)
set(HDF5_USE_STATIC_LIBRARIES OFF)
find_package(HDF5 1.14 REQUIRED COMPONENTS C)
find_package(netCDF 4.9 REQUIRED CONFIG)

if(HDF5_VERSION VERSION_LESS 1.14 OR NOT HDF5_VERSION VERSION_LESS 3)
  message(FATAL_ERROR "MolShredder requires HDF5 >= 1.14 and < 3")
endif()

if(TARGET hdf5-shared)
  set(MOLSHREDDER_HDF5_TARGET hdf5-shared)
elseif(TARGET HDF5::HDF5)
  set(MOLSHREDDER_HDF5_TARGET HDF5::HDF5)
else()
  message(FATAL_ERROR "The HDF5 package did not provide a supported C target")
endif()

include(CTest)
include(FetchContent)
include(GNUInstallDirs)

option(MOLSHREDDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MOLSHREDDER_BUILD_DESKTOP "Build the Qt Quick desktop prototype" OFF)
option(
  MOLSHREDDER_ENABLE_THREAD_SANITIZER
  "Instrument C++ targets with ThreadSanitizer (supported Unix compilers only)"
  OFF
)

set(MOLSHREDDER_SUPPORT_SCHEMA_VERSION 1)
set(MOLSHREDDER_SUPPORT_DESKTOP false)
if(MOLSHREDDER_BUILD_DESKTOP)
  set(MOLSHREDDER_SUPPORT_DESKTOP true)
endif()
set(MOLSHREDDER_SUPPORT_THREAD_SANITIZER false)
if(MOLSHREDDER_ENABLE_THREAD_SANITIZER)
  set(MOLSHREDDER_SUPPORT_THREAD_SANITIZER true)
endif()
if(NOT DEFINED netCDF_VERSION OR netCDF_VERSION STREQUAL "")
  set(netCDF_VERSION "unknown")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "")
  set(MOLSHREDDER_SUPPORT_BUILD_CONFIGURATION "multi-config")
  set(MOLSHREDDER_SUPPORT_CONFIG_IS_MULTI 1)
else()
  string(TOLOWER
    "${CMAKE_BUILD_TYPE}"
    MOLSHREDDER_SUPPORT_BUILD_CONFIGURATION
  )
  set(MOLSHREDDER_SUPPORT_CONFIG_IS_MULTI 0)
endif()
set(
  MOLSHREDDER_SUPPORT_GENERATED_DIR
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  CLI11
  URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.2.tar.gz
  URL_HASH SHA256=c6ea6b2e5608b3ea8617999bd5f47420c71b2ebdb8dc4767c1034d1da5785711
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(CLI11)

find_package(
  Python3 3.9 REQUIRED
  COMPONENTS Interpreter Development.Module Development.Embed
)
file(MAKE_DIRECTORY "${MOLSHREDDER_SUPPORT_GENERATED_DIR}")
configure_file(
  cmake/molshredder_support_configuration.hpp.in
  "${MOLSHREDDER_SUPPORT_GENERATED_DIR}/molshredder_support_configuration.hpp"
  @ONLY
)
configure_file(
  cmake/molshredder-support.json.in
  "${MOLSHREDDER_SUPPORT_GENERATED_DIR}/molshredder-support.json"
  @ONLY
)
set(PYBIND11_FINDPYTHON ON)
set(PYBIND11_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v3.1.0.tar.gz
  URL_HASH SHA256=ef712655692a2e9bf7bb7874c022564a45f91d847ddee987e720cd9e28849665
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pybind11)

set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)
set(MSGPACK_CXX11 OFF CACHE BOOL "" FORCE)
set(MSGPACK_CXX20 ON CACHE BOOL "" FORCE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  FetchContent_Declare(
    msgpack-cxx
    URL https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-8.0.0.tar.gz
    URL_HASH SHA256=f634fb7052da4478096f2a02dfb6d91174e5836b317afb006375249ccb086aa8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(msgpack-cxx)
else()
  FetchContent_Declare(
    msgpack-cxx
    URL https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-8.0.0.tar.gz
    URL_HASH SHA256=f634fb7052da4478096f2a02dfb6d91174e5836b317afb006375249ccb086aa8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(
    msgpack-cxx
    SOURCE_DIR MOLSHREDDER_MSGPACK_SOURCE_DIR
    BINARY_DIR MOLSHREDDER_MSGPACK_BINARY_DIR
    POPULATED MOLSHREDDER_MSGPACK_POPULATED
  )
  if(NOT MOLSHREDDER_MSGPACK_POPULATED)
    FetchContent_Populate(msgpack-cxx)
    FetchContent_GetProperties(
      msgpack-cxx
      SOURCE_DIR MOLSHREDDER_MSGPACK_SOURCE_DIR
      BINARY_DIR MOLSHREDDER_MSGPACK_BINARY_DIR
    )
  endif()
  add_subdirectory(
    "${MOLSHREDDER_MSGPACK_SOURCE_DIR}"
    "${MOLSHREDDER_MSGPACK_BINARY_DIR}"
    EXCLUDE_FROM_ALL
  )
endif()

if(MOLSHREDDER_ENABLE_THREAD_SANITIZER)
  if(WIN32)
    message(
      FATAL_ERROR
      "MOLSHREDDER_ENABLE_THREAD_SANITIZER is not supported on Windows"
    )
  endif()
  include(CheckCXXSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
  check_cxx_source_compiles(
    "int main() { return 0; }"
    MOLSHREDDER_COMPILER_SUPPORTS_THREAD_SANITIZER
  )
  cmake_pop_check_state()
  if(NOT MOLSHREDDER_COMPILER_SUPPORTS_THREAD_SANITIZER)
    message(
      FATAL_ERROR
      "The selected C++ compiler cannot compile and link ThreadSanitizer"
    )
  endif()
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=thread>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>
  )
  add_link_options(-fsanitize=thread)
endif()

function(molshredder_enable_warnings target_name)
  if(MSVC)
    target_compile_options(
      ${target_name} PRIVATE /W4 /permissive- /external:anglebrackets /external:W0
    )
    if(MOLSHREDDER_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE /WX)
    endif()
  else()
    target_compile_options(
      ${target_name}
      PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
    if(MOLSHREDDER_WARNINGS_AS_ERRORS)
      target_compile_options(${target_name} PRIVATE -Werror)
    endif()
  endif()
endfunction()

if(BUILD_TESTING)
  foreach(
    required_pdb_fixture
    IN ITEMS
      tests/io/fixtures/synthetic_multimodel.pdb
      tests/io/fixtures/pick_single_atom.pdb
      tests/io/fixtures/lammps_topology.pdb
      tests/io/fixtures/1ubq.pdb
      tests/application/fixtures/pbc_distance.pdb
      tests/application/fixtures/hydrogen_bond.pdb
  )
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${required_pdb_fixture}")
      message(
        FATAL_ERROR
        "Required test fixture is missing from the source tree: ${required_pdb_fixture}"
      )
    endif()
  endforeach()

  add_executable(
    molshredder_bcif_fixture_generator
    tests/support/bcif_fixture_generator.cpp
  )
  target_compile_features(molshredder_bcif_fixture_generator PRIVATE cxx_std_20)
  target_link_libraries(molshredder_bcif_fixture_generator PRIVATE msgpack-cxx)
  target_include_directories(
    molshredder_bcif_fixture_generator
    SYSTEM PRIVATE
      "${msgpack-cxx_SOURCE_DIR}/include"
      "${msgpack-cxx_BINARY_DIR}/include"
  )
  molshredder_enable_warnings(molshredder_bcif_fixture_generator)
  set(MOLSHREDDER_BCIF_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/structure.bcif")
  set(MOLSHREDDER_BCIF_OLD_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/structure-old.bcif")
  add_test(
    NAME io.bcif_fixture_setup
    COMMAND molshredder_bcif_fixture_generator
      "${MOLSHREDDER_BCIF_FIXTURE}"
      "${MOLSHREDDER_BCIF_OLD_FIXTURE}"
  )
  set_tests_properties(
    io.bcif_fixture_setup PROPERTIES FIXTURES_SETUP bcif_fixture
  )

  add_executable(
    molshredder_h5md_fixture_generator
    tests/support/h5md_fixture_generator.cpp
  )
  target_compile_features(molshredder_h5md_fixture_generator PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_h5md_fixture_generator PRIVATE ${MOLSHREDDER_HDF5_TARGET}
  )
  molshredder_enable_warnings(molshredder_h5md_fixture_generator)
  set(MOLSHREDDER_H5MD_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/trajectory.h5md")
  set(MOLSHREDDER_H5MD_FIXED_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-fixed.h5md")
  set(MOLSHREDDER_H5MD_AMBIGUOUS_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-ambiguous.h5md")
  set(MOLSHREDDER_H5MD_PARTIAL_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-partial-periodic.h5md")
  set(MOLSHREDDER_H5MD_MISSING_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-missing.h5md")
  set(MOLSHREDDER_H5MD_DYNAMIC_ID_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-dynamic-id.h5md")
  add_test(
    NAME io.h5md_fixture_setup
    COMMAND molshredder_h5md_fixture_generator
      "${MOLSHREDDER_H5MD_FIXTURE}"
      "${MOLSHREDDER_H5MD_FIXED_FIXTURE}"
      "${MOLSHREDDER_H5MD_AMBIGUOUS_FIXTURE}"
      "${MOLSHREDDER_H5MD_PARTIAL_FIXTURE}"
      "${MOLSHREDDER_H5MD_MISSING_FIXTURE}"
      "${MOLSHREDDER_H5MD_DYNAMIC_ID_FIXTURE}"
  )
  set_tests_properties(
    io.h5md_fixture_setup PROPERTIES FIXTURES_SETUP h5md_fixture
  )

  add_executable(
    molshredder_amber_netcdf_fixture_generator
    tests/support/amber_netcdf_fixture_generator.cpp
  )
  target_compile_features(
    molshredder_amber_netcdf_fixture_generator PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_amber_netcdf_fixture_generator PRIVATE netCDF::netcdf
  )
  molshredder_enable_warnings(molshredder_amber_netcdf_fixture_generator)
  set(MOLSHREDDER_AMBER_NETCDF_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory.nc")
  set(MOLSHREDDER_AMBER_NETCDF_COMPRESSED_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-compressed.ncdf")
  set(MOLSHREDDER_AMBER_NETCDF_BAD_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-bad-convention.nc")
  set(MOLSHREDDER_AMBER_NETCDF_PARTIAL_CELL_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-partial-cell.nc")
  set(MOLSHREDDER_AMBER_NETCDF_MISSING_DATA_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/trajectory-missing-data.nc")
  set(MOLSHREDDER_AMBER_NETCDF_RESTART_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/restart.ncrst")
  add_test(
    NAME io.amber_netcdf_fixture_setup
    COMMAND molshredder_amber_netcdf_fixture_generator
      "${MOLSHREDDER_AMBER_NETCDF_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_COMPRESSED_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_BAD_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_PARTIAL_CELL_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_MISSING_DATA_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_RESTART_FIXTURE}"
  )
  set_tests_properties(
    io.amber_netcdf_fixture_setup
    PROPERTIES FIXTURES_SETUP amber_netcdf_fixture
  )

  add_executable(
    molshredder_binpos_fixture_generator
    tests/support/binpos_fixture_generator.cpp
  )
  target_compile_features(
    molshredder_binpos_fixture_generator PRIVATE cxx_std_20
  )
  molshredder_enable_warnings(molshredder_binpos_fixture_generator)
  set(
    MOLSHREDDER_BINPOS_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/trajectory.binpos"
  )
  add_test(
    NAME io.binpos_fixture_setup
    COMMAND
      molshredder_binpos_fixture_generator "${MOLSHREDDER_BINPOS_FIXTURE}"
  )
  set_tests_properties(
    io.binpos_fixture_setup
    PROPERTIES FIXTURES_SETUP binpos_fixture
  )

  add_executable(
    molshredder_mrc_fixture_generator
    tests/support/mrc_fixture_generator.cpp
  )
  target_compile_features(molshredder_mrc_fixture_generator PRIVATE cxx_std_20)
  molshredder_enable_warnings(molshredder_mrc_fixture_generator)
  set(MOLSHREDDER_MRC_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/density.mrc")
  set(MOLSHREDDER_MRC_BIG_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/density-big.ccp4")
  add_test(
    NAME io.mrc_fixture_setup
    COMMAND molshredder_mrc_fixture_generator
      "${MOLSHREDDER_MRC_FIXTURE}"
      "${MOLSHREDDER_MRC_BIG_FIXTURE}"
  )
  set_tests_properties(
    io.mrc_fixture_setup PROPERTIES FIXTURES_SETUP mrc_fixture
  )
endif()

add_library(molshredder_core STATIC)
add_library(MolShredder::Core ALIAS molshredder_core)

target_sources(
  molshredder_core
  PRIVATE
    src/analysis/alignment.cpp
    src/analysis/basic.cpp
    src/analysis/contacts.cpp
    src/analysis/principal_axes.cpp
    src/analysis/secondary_structure.cpp
    src/analysis/time_series.cpp
    src/application/default_registry.cpp
    src/application/analysis_result.cpp
    src/application/dispatcher.cpp
    src/application/runtime_diagnostics.cpp
    src/application/session.cpp
    src/application/task_service.cpp
    src/application/representation_visibility.cpp
    src/application/workspace.cpp
    src/command/foundation_grammar.cpp
    src/command/registry.cpp
    src/command/result.cpp
    src/command/serialization.cpp
    src/core/version.cpp
    src/io/mmcif_reader.cpp
    src/io/mmcif_writer.cpp
    src/io/bcif_reader.cpp
    src/io/amber_netcdf_reader.cpp
    src/io/amber_ascii_reader.cpp
    src/io/binpos_reader.cpp
    src/io/lammps_dump_reader.cpp
    src/io/molfile_provider.cpp
    src/io/molfile_reader.cpp
    src/io/mol2_reader.cpp
    src/io/psf_reader.cpp
    src/io/prmtop_reader.cpp
    src/io/gro_reader.cpp
    src/io/g96_reader.cpp
    src/io/h5md_reader.cpp
    src/io/vtf_reader.cpp
    src/io/format_capabilities.cpp
    src/io/dcd_reader.cpp
    src/io/trr_reader.cpp
    src/io/xtc_reader.cpp
    src/io/rst7_reader.cpp
    src/io/pdb_reader.cpp
    src/io/pqr_reader.cpp
    src/io/structure_reader.cpp
    src/io/structure_writer.cpp
    src/io/xyz_reader.cpp
    src/io/trajectory_reader.cpp
    src/io/trajectory_writer.cpp
    src/io/mrc_reader.cpp
    src/io/volume_reader.cpp
    src/io/volume_writer.cpp
    src/model/coordinates.cpp
    src/model/molecular_system.cpp
    src/model/topology.cpp
    src/model/volume.cpp
    src/operation/task_scheduler.cpp
    src/render/reference_renderer.cpp
    src/render/representation.cpp
    src/render/setting_store.cpp
    src/render/volume_isosurface.cpp
    src/selection/evaluator.cpp
    src/selection/expression.cpp
    src/selection/named_selection.cpp
    src/trajectory/frame_cache.cpp
    src/trajectory/attachment.cpp
    src/trajectory/prefetch_scheduler.cpp
    src/trajectory/playback.cpp
    src/trajectory/pbc.cpp
    src/scene/camera.cpp
    src/scene/stereo.cpp
    src/scene/pymol_view.cpp
    src/scene/math.cpp
    src/scene/scene.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/molshredder/analysis/alignment.hpp
      include/molshredder/analysis/basic.hpp
      include/molshredder/analysis/contacts.hpp
      include/molshredder/analysis/principal_axes.hpp
      include/molshredder/analysis/secondary_structure.hpp
      include/molshredder/analysis/time_series.hpp
      include/molshredder/application/default_registry.hpp
      include/molshredder/application/analysis_result.hpp
      include/molshredder/application/dispatcher.hpp
      include/molshredder/application/runtime_diagnostics.hpp
      include/molshredder/application/representation_visibility.hpp
      include/molshredder/application/session.hpp
      include/molshredder/application/task_service.hpp
      include/molshredder/application/workspace.hpp
      include/molshredder/command/foundation_grammar.hpp
      include/molshredder/operation/common_types.hpp
      include/molshredder/operation/error.hpp
      include/molshredder/operation/operation.hpp
      include/molshredder/operation/result.hpp
      include/molshredder/operation/task_context.hpp
      include/molshredder/operation/task_scheduler.hpp
      include/molshredder/command/registry.hpp
      include/molshredder/command/result.hpp
      include/molshredder/command/serialization.hpp
      include/molshredder/io/structure_reader.hpp
      include/molshredder/io/structure_writer.hpp
      include/molshredder/io/molfile_provider.hpp
      include/molshredder/io/format_capabilities.hpp
      include/molshredder/io/trajectory_reader.hpp
      include/molshredder/io/trajectory_writer.hpp
      include/molshredder/io/volume_reader.hpp
      include/molshredder/io/volume_writer.hpp
      include/molshredder/model/coordinates.hpp
      include/molshredder/model/molecular_system.hpp
      include/molshredder/model/topology.hpp
      include/molshredder/trajectory/attachment.hpp
      include/molshredder/scene/stereo.hpp
      include/molshredder/model/volume.hpp
      include/molshredder/render/packet.hpp
      include/molshredder/render/reference_renderer.hpp
      include/molshredder/render/representation.hpp
      include/molshredder/render/setting_store.hpp
      include/molshredder/render/volume_isosurface.hpp
      include/molshredder/selection/evaluator.hpp
      include/molshredder/selection/expression.hpp
      include/molshredder/selection/named_selection.hpp
      include/molshredder/trajectory/frame_cache.hpp
      include/molshredder/trajectory/prefetch_scheduler.hpp
      include/molshredder/trajectory/playback.hpp
      include/molshredder/trajectory/pbc.hpp
      include/molshredder/scene/camera.hpp
      include/molshredder/scene/pymol_view.hpp
      include/molshredder/scene/math.hpp
      include/molshredder/scene/scene.hpp
      include/molshredder/version.hpp
)

target_compile_features(molshredder_core PUBLIC cxx_std_20)
target_link_libraries(
  molshredder_core PUBLIC Threads::Threads netCDF::netcdf
                           ${MOLSHREDDER_HDF5_TARGET}
                  PRIVATE ${CMAKE_DL_LIBS}
)
target_include_directories(
  molshredder_core
  PRIVATE
    "${MOLSHREDDER_SUPPORT_GENERATED_DIR}"
)
target_include_directories(
  molshredder_core
  SYSTEM PRIVATE
    "${msgpack-cxx_SOURCE_DIR}/include"
    "${msgpack-cxx_BINARY_DIR}/include"
)
target_compile_definitions(
  molshredder_core PRIVATE MSGPACK_NO_BOOST MSGPACK_DEFAULT_API_VERSION=3
)
target_compile_definitions(
  molshredder_core
  PRIVATE MOLSHREDDER_VERSION="${PROJECT_VERSION}"
)
set_target_properties(
  molshredder_core
  PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

molshredder_enable_warnings(molshredder_core)

add_library(molshredder_automation STATIC)
add_library(MolShredder::Automation ALIAS molshredder_automation)
target_sources(
  molshredder_automation
  PRIVATE
    src/automation/python_script.cpp
    src/python/bindings.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/molshredder/automation/python_script.hpp
)
target_include_directories(
  molshredder_automation
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/python"
    ${Python3_INCLUDE_DIRS}
    ${pybind11_INCLUDE_DIRS}
)
target_compile_features(molshredder_automation PUBLIC cxx_std_20)
target_link_libraries(molshredder_automation PUBLIC MolShredder::Core)
set_target_properties(
  molshredder_automation
  PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON
)
molshredder_enable_warnings(molshredder_automation)

add_library(molshredder_embedded_python STATIC src/python/embedded_module.cpp)
target_include_directories(
  molshredder_embedded_python PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/python"
)
target_compile_features(molshredder_embedded_python PRIVATE cxx_std_20)
target_link_libraries(
  molshredder_embedded_python PUBLIC MolShredder::Automation
                              PRIVATE pybind11::embed
)
set_target_properties(
  molshredder_embedded_python
  PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON
)
molshredder_enable_warnings(molshredder_embedded_python)

add_library(molshredder_gui STATIC)
add_library(MolShredder::Gui ALIAS molshredder_gui)
target_sources(
  molshredder_gui
  PRIVATE
    src/gui/action_adapter.cpp
    src/gui/analysis_presenter.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/molshredder/gui/action_adapter.hpp
      include/molshredder/gui/analysis_presenter.hpp
)
target_compile_features(molshredder_gui PUBLIC cxx_std_20)
target_link_libraries(molshredder_gui PUBLIC MolShredder::Core)
set_target_properties(
  molshredder_gui
  PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
molshredder_enable_warnings(molshredder_gui)

add_library(molshredder_cli STATIC)
add_library(MolShredder::Cli ALIAS molshredder_cli)
target_sources(
  molshredder_cli
  PRIVATE
    src/cli/adapter.cpp
    src/cli/console.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/molshredder/cli/adapter.hpp
      include/molshredder/cli/console.hpp
)
target_compile_features(molshredder_cli PUBLIC cxx_std_20)
target_link_libraries(
  molshredder_cli
  PUBLIC MolShredder::Core
  PRIVATE CLI11::CLI11
)
molshredder_enable_warnings(molshredder_cli)

add_executable(molshredder apps/cli/main.cpp)
target_compile_features(molshredder PRIVATE cxx_std_20)
target_include_directories(
  molshredder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/python"
)
target_link_libraries(
  molshredder PRIVATE MolShredder::Cli molshredder_embedded_python
)
set_target_properties(molshredder PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
molshredder_enable_warnings(molshredder)

pybind11_add_module(molshredder_python MODULE src/python/module.cpp)
set_target_properties(
  molshredder_python
  PROPERTIES
    OUTPUT_NAME molshredder
    CXX_EXTENSIONS OFF
    INSTALL_RPATH_USE_LINK_PATH TRUE
)
target_compile_features(molshredder_python PRIVATE cxx_std_20)
target_include_directories(
  molshredder_python PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/python"
)
target_link_libraries(molshredder_python PRIVATE MolShredder::Automation)
molshredder_enable_warnings(molshredder_python)

if(MOLSHREDDER_BUILD_DESKTOP)
  find_package(
    Qt6 6.8.3 EXACT REQUIRED
    COMPONENTS Core Gui LinguistTools Qml Quick ShaderTools
  )
  qt_standard_project_setup(REQUIRES 6.8)
  qt_add_executable(
    molshredder_desktop
    MACOSX_BUNDLE
    WIN32
    apps/desktop/localization_controller.cpp
    apps/desktop/localization_controller.hpp
    apps/desktop/main.cpp
    apps/desktop/redirected_render_smoke.cpp
    apps/desktop/redirected_render_smoke.hpp
    apps/desktop/viewport_item.cpp
    apps/desktop/viewport_item.hpp
  )
  target_compile_features(molshredder_desktop PRIVATE cxx_std_20)
  target_include_directories(
    molshredder_desktop
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/apps/desktop"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/python"
  )
  target_link_libraries(
    molshredder_desktop
    PRIVATE
      MolShredder::Gui
      molshredder_embedded_python
      Qt6::Core
      Qt6::Gui
      Qt6::GuiPrivate
      Qt6::Qml
      Qt6::Quick
  )
  set_target_properties(
    molshredder_desktop PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE
  )
  molshredder_enable_warnings(molshredder_desktop)
  if(MSVC)
    # Qt 6.8 inline headers currently trigger C4702 under MSVC. Keep /WX for
    # MolShredder diagnostics while suppressing that third-party-only warning
    # on the Qt-facing Desktop target.
    target_compile_options(molshredder_desktop PRIVATE /wd4702)
  endif()
  set_source_files_properties(
    apps/desktop/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml
  )
  qt_add_qml_module(
    molshredder_desktop
    URI MolShredder.Desktop
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/MolShredder/Desktop"
    QML_FILES apps/desktop/Main.qml
    NO_RESOURCE_TARGET_PATH
  )
  qt_add_translations(
    molshredder_desktop
    TS_FILES translations/molshredder_ko.ts
    RESOURCE_PREFIX /i18n
  )
  qt_add_shaders(
    molshredder_desktop
    molshredder_desktop_shaders
    PRECOMPILE
    OPTIMIZED
    BASE apps/desktop
    PREFIX /molshredder/desktop
    FILES
      apps/desktop/shaders/molecule.vert
      apps/desktop/shaders/molecule.frag
      apps/desktop/shaders/sphere.vert
      apps/desktop/shaders/cylinder.vert
      apps/desktop/shaders/solid.frag
      apps/desktop/shaders/line.vert
      apps/desktop/shaders/line.frag
      apps/desktop/shaders/pick.frag
      apps/desktop/shaders/pick_mesh.vert
      apps/desktop/shaders/pick_sphere.vert
      apps/desktop/shaders/pick_cylinder.vert
      apps/desktop/shaders/pick_line.vert
      apps/desktop/shaders/stereo_composite.vert
      apps/desktop/shaders/stereo_composite.frag
  )
  install(
    TARGETS molshredder_desktop
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
  if(BUILD_TESTING)
    if(WIN32)
      # The Windows CI test build must remain attached to CTest's console so
      # deterministic qInfo/qCritical smoke evidence can be captured. Release
      # builds keep the normal GUI subsystem selected by qt_add_executable.
      set_target_properties(molshredder_desktop PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    add_test(
      NAME frontend.localization_catalog
      COMMAND
        Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/localization_catalog_test.py"
        --qml "${CMAKE_CURRENT_SOURCE_DIR}/apps/desktop/Main.qml"
        --ts "${CMAKE_CURRENT_SOURCE_DIR}/translations/molshredder_ko.ts"
    )
    add_test(
      NAME desktop.localization_english_smoke
      COMMAND molshredder_desktop --smoke --language=en
    )
    set_tests_properties(
      desktop.localization_english_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION
          "MolShredder localization ready: language=en title=MolShredder Molecular Viewer"
    )
    add_test(
      NAME desktop.localization_korean_smoke
      COMMAND molshredder_desktop --smoke --language=ko
    )
    set_tests_properties(
      desktop.localization_korean_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION
          "MolShredder localization ready: language=ko title=MolShredder 분자 뷰어"
    )
    if(WIN32)
      set(MOLSHREDDER_REDIRECTED_RENDER_BACKEND d3d11)
    elseif(APPLE)
      set(MOLSHREDDER_REDIRECTED_RENDER_BACKEND metal)
    else()
      set(MOLSHREDDER_REDIRECTED_RENDER_BACKEND opengl)
    endif()
    add_executable(
      molshredder_dcd_fixture_generator
      tests/support/dcd_fixture_generator.cpp
    )
    target_compile_features(
      molshredder_dcd_fixture_generator PRIVATE cxx_std_20
    )
    molshredder_enable_warnings(molshredder_dcd_fixture_generator)
    set(
      MOLSHREDDER_DESKTOP_DCD_FIXTURE
      "${CMAKE_CURRENT_BINARY_DIR}/desktop_trajectory.dcd"
    )
    add_test(
      NAME desktop.trajectory_fixture_setup
      COMMAND
        molshredder_dcd_fixture_generator
        "${MOLSHREDDER_DESKTOP_DCD_FIXTURE}"
    )
    set_tests_properties(
      desktop.trajectory_fixture_setup
      PROPERTIES FIXTURES_SETUP desktop_trajectory_fixture
    )
    add_test(
      NAME desktop.redirected_renderer_smoke
      COMMAND
        molshredder_desktop
        "--redirected-render-smoke=${MOLSHREDDER_REDIRECTED_RENDER_BACKEND}"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--trajectory=${MOLSHREDDER_DESKTOP_DCD_FIXTURE}"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.redirected_renderer_smoke
      PROPERTIES
        FIXTURES_REQUIRED desktop_trajectory_fixture
        TIMEOUT 20
        PASS_REGULAR_EXPRESSION
          "MolShredder redirected renderer ready: requested=${MOLSHREDDER_REDIRECTED_RENDER_BACKEND} .*atoms=3 frames=4 frame=1 variation=true deterministic=true picking=true"
    )
    if(WIN32)
      set_tests_properties(
        desktop.redirected_renderer_smoke
        PROPERTIES
          ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=rhi;QSG_RHI_PREFER_SOFTWARE_RENDERER=1"
      )
    endif()
    add_test(NAME desktop.gpu_smoke COMMAND molshredder_desktop --smoke)
    set_tests_properties(
      desktop.gpu_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder GPU primitives ready: mesh=[1-9][0-9]* lines=[1-9][0-9]* cylinders=[1-9][0-9]* spheres=[1-9][0-9]*"
    )
    add_test(
      NAME desktop.graphics_info_smoke
      COMMAND molshredder_desktop --smoke --graphics-info-smoke
    )
    set_tests_properties(
      desktop.graphics_info_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop graphics diagnostics ready"
    )
    add_test(
      NAME desktop.system_info_panel_smoke
      COMMAND molshredder_desktop --smoke --system-info-panel-smoke
    )
    set_tests_properties(
      desktop.system_info_panel_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder system information panel ready: typed-source=exact"
    )
    add_test(
      NAME desktop.named_view_smoke
      COMMAND molshredder_desktop --smoke --named-view-smoke
    )
    set_tests_properties(
      desktop.named_view_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop named view ready: stored=1 recalled=animated-exact panel=visible pymol18=animated-roundtrip"
    )
    add_test(
      NAME desktop.stereo_smoke
      COMMAND molshredder_desktop --smoke --stereo-smoke
    )
    set_tests_properties(
      desktop.stereo_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop stereo ready: mode=side_by_side eyes=2 shift=2.0 angle=2.1"
    )
    add_test(
      NAME desktop.anaglyph_smoke
      COMMAND molshredder_desktop --smoke --anaglyph-smoke
    )
    set_tests_properties(
      desktop.anaglyph_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop anaglyph ready: mode=optimized eyes=2 compositor=QRhi"
    )
    add_test(
      NAME desktop.interleaved_smoke
      COMMAND molshredder_desktop --smoke --interleaved-smoke
    )
    set_tests_properties(
      desktop.interleaved_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop interleaved ready: modes=3 final=checkerboard eyes=2 compositor=QRhi"
    )
    add_test(
      NAME desktop.structure_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--camera-smoke"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=sticks"
    )
    add_test(
      NAME desktop.batch_load_smoke
      COMMAND
        molshredder_desktop --smoke --batch-load-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
    )
    set_tests_properties(
      desktop.batch_load_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop atomic batch load ready: inputs=2 objects=2 canonical=shared"
    )
    add_test(
      NAME desktop.representation_visibility_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=sticks"
        "--representation-visibility-smoke"
    )
    set_tests_properties(
      desktop.representation_visibility_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop representation visibility ready: operations=4 canonical=shared"
    )
    add_test(
      NAME desktop.render_setting_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=spheres"
        "--render-setting-smoke"
    )
    set_tests_properties(
      desktop.render_setting_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop render setting ready: typed=shared geometry=updated editor=visible"
    )
    add_test(
      NAME desktop.analysis_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=sticks"
        "--analysis-smoke"
    )
    set_tests_properties(
      desktop.analysis_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop analysis ready: results=2 overlay=marker-dash-label canonical=shared panel=visible"
    )
    set_tests_properties(
      desktop.structure_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop camera ready: representation=sticks atoms=3"
    )
    add_test(
      NAME desktop.protein_structure_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--camera-smoke"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/1ubq.pdb"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.protein_structure_open_smoke
      PROPERTIES
        TIMEOUT 15
        PASS_REGULAR_EXPRESSION "MolShredder desktop camera ready: representation=spheres atoms=660"
    )
    add_test(
      NAME desktop.python_script_smoke
      COMMAND
        molshredder_desktop --smoke --script-smoke
        "--script=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation/fixtures/gui_success.py"
    )
    set_tests_properties(
      desktop.python_script_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop Python script ready: objects=1 atoms=3 output=captured async=true"
    )
    add_test(
      NAME desktop.python_script_cancel_smoke
      COMMAND
        molshredder_desktop --smoke --script-cancel-smoke
        "--script=${CMAKE_CURRENT_SOURCE_DIR}/tests/automation/fixtures/gui_cancel.py"
    )
    set_tests_properties(
      desktop.python_script_cancel_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop Python script cancellation ready: checkpoint=post-execution output=captured"
    )
    add_test(
      NAME desktop.mmcif_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--camera-smoke"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.cif"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.mmcif_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop camera ready: representation=spheres atoms=3"
    )
    add_test(
      NAME desktop.bcif_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${MOLSHREDDER_BCIF_FIXTURE}"
        "--representation=sticks"
    )
    add_test(
      NAME desktop.opendx_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/potential.dx"
    )
    set_tests_properties(
      desktop.opendx_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop isosurface ready: level=3.5 vertices="
    )
    add_test(
      NAME desktop.mrc_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${MOLSHREDDER_MRC_FIXTURE}"
    )
    set_tests_properties(
      desktop.mrc_open_smoke
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED mrc_fixture
        PASS_REGULAR_EXPRESSION "MolShredder desktop isosurface ready: level=56 vertices="
    )
    set_tests_properties(
      desktop.bcif_open_smoke
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED bcif_fixture
        PASS_REGULAR_EXPRESSION "MolShredder desktop loaded: atoms=2 representation=sticks"
    )
    add_test(
      NAME desktop.gpu_picking_smoke
      COMMAND
        molshredder_desktop --picking-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/pick_single_atom.pdb"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.gpu_picking_smoke
      PROPERTIES
        TIMEOUT 15
        PASS_REGULAR_EXPRESSION "MolShredder desktop GPU picking ready: Atom 1"
    )
    add_test(
      NAME desktop.object_panel_smoke
      COMMAND
        molshredder_desktop --smoke --object-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/pick_single_atom.pdb"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.object_panel_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop objects ready: objects=1 active=2 visible=0 rename-delete-reorder=true spheres=0"
    )
    add_test(
      NAME desktop.trajectory_smoke
      COMMAND
        molshredder_desktop --trajectory-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--trajectory=${MOLSHREDDER_DESKTOP_DCD_FIXTURE}"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.trajectory_smoke
      PROPERTIES
        FIXTURES_REQUIRED desktop_trajectory_fixture
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop trajectory ready: frames=4 scheduler=qt mode=loop"
    )
    add_test(
      NAME desktop.daily_workflow_smoke
      COMMAND
        molshredder_desktop --daily-workflow-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--trajectory=${MOLSHREDDER_DESKTOP_DCD_FIXTURE}"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.daily_workflow_smoke
      PROPERTIES
        FIXTURES_REQUIRED desktop_trajectory_fixture
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop daily workflow ready: open=1 representation=sticks selection=1 analysis=2 trajectory=4 exports=3 cancellation=preserved"
    )
    add_test(
      NAME desktop.xyz_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-all --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/multiframe.xyz"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.xyz"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.xyz_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop XYZ save ready: atoms=3 frames=2 loss-report=true"
    )
    add_test(
      NAME desktop.pdb_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-all --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.pdb"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.pdb_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop PDB save ready: atoms=3 frames=2 loss-report=true"
    )
    add_test(
      NAME desktop.mmcif_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-all --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.cif"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.cif"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.mmcif_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop MMCIF save ready: atoms=3 frames=2 loss-report=true"
    )
    add_test(
      NAME desktop.pqr_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.pqr"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.pqr_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop PQR save ready: atoms=3 frames=1 loss-report=true"
    )
    add_test(
      NAME desktop.sdf_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.sdf"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.sdf"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.sdf_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop SDF save ready: atoms=2 frames=1 loss-report=true"
    )
    add_test(
      NAME desktop.mol2_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.mol2"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.mol2"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.mol2_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop MOL2 save ready: atoms=6 frames=1 loss-report=true"
    )
    add_test(
      NAME desktop.gro_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.gro"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.gro"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.gro_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop GRO save ready: atoms=2 frames=1 loss-report=true"
    )
    add_test(
      NAME desktop.g96_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-all --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.g96"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.g96_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop G96 save ready: atoms=2 frames=2 loss-report=true"
    )
    add_test(
      NAME desktop.vtf_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/coarse_grained.vtf"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.vtf_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop loaded: atoms=3 representation=sticks"
    )
    add_test(
      NAME desktop.psf_save_smoke
      COMMAND
        molshredder_desktop --smoke --save-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.psf"
        "--save=${CMAKE_CURRENT_BINARY_DIR}/desktop_saved.psf"
    )
    set_tests_properties(
      desktop.psf_save_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop PSF save ready: atoms=4 frames=0 loss-report=true"
    )
    add_test(
      NAME desktop.amber_open_smoke
      COMMAND
        molshredder_desktop --smoke --amber-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
        "--trajectory=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/frame.rst7"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.amber_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop Amber ready: atoms=4 frames=1 representation=sticks"
    )
    add_test(
      NAME desktop.amber_mdcrd_smoke
      COMMAND
        molshredder_desktop --smoke --amber-mdcrd-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
        "--trajectory=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.mdcrd"
        "--representation=sticks"
    )
    add_test(
      NAME desktop.amber_netcdf_smoke
      COMMAND
        molshredder_desktop --smoke --amber-netcdf-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
        "--trajectory=${MOLSHREDDER_AMBER_NETCDF_FIXTURE}"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.amber_netcdf_smoke
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED amber_netcdf_fixture
        PASS_REGULAR_EXPRESSION "MolShredder desktop Amber NetCDF ready: atoms=4 frames=2 representation=sticks"
    )
    add_test(
      NAME desktop.h5md_smoke
      COMMAND
        molshredder_desktop --smoke --h5md-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
        "--trajectory=${MOLSHREDDER_H5MD_FIXTURE}"
        "--trajectory-mapping=exact"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.h5md_smoke
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED h5md_fixture
        PASS_REGULAR_EXPRESSION "MolShredder desktop H5MD ready: atoms=3 frames=2 mapping=source-id unit=angstrom"
    )
    set_tests_properties(
      desktop.amber_mdcrd_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop Amber MDCRD ready: atoms=4 frames=2 representation=sticks"
    )
    add_test(
      NAME desktop.lammps_smoke
      COMMAND
        molshredder_desktop --smoke --lammps-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
        "--trajectory=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.lammpstrj"
        "--trajectory-unit=angstrom"
        "--trajectory-mapping=exact"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.lammps_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop LAMMPS ready: atoms=3 frames=2 mapping=source-id unit=angstrom"
    )
    add_test(
      NAME desktop.binpos_smoke
      COMMAND
        molshredder_desktop --smoke --binpos-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
        "--trajectory=${MOLSHREDDER_BINPOS_FIXTURE}"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.binpos_smoke
      PROPERTIES
        TIMEOUT 10
        FIXTURES_REQUIRED binpos_fixture
        PASS_REGULAR_EXPRESSION "MolShredder desktop BINPOS ready: atoms=3 frames=2 precision=float32"
    )
    get_property(
      MOLSHREDDER_DESKTOP_TESTS
      DIRECTORY
      PROPERTY TESTS
    )
    foreach(MOLSHREDDER_DESKTOP_TEST IN LISTS MOLSHREDDER_DESKTOP_TESTS)
      if(MOLSHREDDER_DESKTOP_TEST MATCHES "^desktop\\.")
        set_property(
          TEST "${MOLSHREDDER_DESKTOP_TEST}"
          PROPERTY FAIL_REGULAR_EXPRESSION
            "qrc:/.*(TypeError|ReferenceError)|QQmlApplicationEngine failed"
        )
      endif()
    endforeach()
  endif()
endif()

install(
  TARGETS
    molshredder
    molshredder_core
    molshredder_automation
    molshredder_gui
    molshredder_cli
  EXPORT MolShredderTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
  TARGETS molshredder_python
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/molshredder/python"
  RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/molshredder/python"
)

install(
  FILES
    docs/schemas/capability-support-v1.schema.json
    docs/schemas/result-envelope-v1.schema.json
    docs/schemas/result-envelope-v2.schema.json
    docs/schemas/support-configuration-v1.schema.json
  DESTINATION "${CMAKE_INSTALL_DATADIR}/molshredder/schemas"
)
install(
  FILES "${MOLSHREDDER_SUPPORT_GENERATED_DIR}/molshredder-support.json"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/molshredder"
)
install(
  FILES LICENSE THIRD_PARTY_NOTICES.md TRADEMARKS.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/molshredder"
)

if(BUILD_TESTING)
  add_executable(molshredder_core_tests tests/core/version_test.cpp)
  target_compile_features(molshredder_core_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_core_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_core_tests)

  add_test(NAME core.version COMMAND molshredder_core_tests)

  add_executable(molshredder_operation_tests tests/operation/operation_test.cpp)
  target_compile_features(molshredder_operation_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_operation_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_operation_tests)
  add_test(NAME operation.contract COMMAND molshredder_operation_tests)

  add_executable(
    molshredder_task_scheduler_tests
    tests/operation/task_scheduler_test.cpp
  )
  target_compile_features(molshredder_task_scheduler_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_task_scheduler_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_task_scheduler_tests)
  add_test(NAME operation.task_scheduler COMMAND molshredder_task_scheduler_tests)

  add_executable(molshredder_topology_tests tests/model/topology_test.cpp)
  target_compile_features(molshredder_topology_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_topology_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_topology_tests)
  add_test(NAME model.topology COMMAND molshredder_topology_tests)

  add_executable(molshredder_coordinates_tests tests/model/coordinates_test.cpp)
  target_compile_features(molshredder_coordinates_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_coordinates_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_coordinates_tests)
  add_test(NAME model.coordinates COMMAND molshredder_coordinates_tests)

  add_executable(
    molshredder_molecular_system_tests
    tests/model/molecular_system_test.cpp
  )
  target_compile_features(
    molshredder_molecular_system_tests
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_molecular_system_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_molecular_system_tests)
  add_test(NAME model.molecular_system COMMAND molshredder_molecular_system_tests)

  add_executable(molshredder_volume_tests tests/model/volume_test.cpp)
  target_compile_features(molshredder_volume_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_volume_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_volume_tests)
  add_test(NAME model.volume COMMAND molshredder_volume_tests)

  add_executable(
    molshredder_opendx_reader_tests
    tests/io/opendx_reader_test.cpp
  )
  target_compile_features(
    molshredder_opendx_reader_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_opendx_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_opendx_reader_tests)
  add_test(
    NAME io.opendx_reader
    COMMAND molshredder_opendx_reader_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/potential.dx"
  )

  add_executable(
    molshredder_opendx_writer_tests
    tests/io/opendx_writer_test.cpp
  )
  target_compile_features(
    molshredder_opendx_writer_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_opendx_writer_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_opendx_writer_tests)
  add_test(
    NAME io.opendx_writer
    COMMAND molshredder_opendx_writer_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/potential.dx"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_mrc_reader_tests
    tests/io/mrc_reader_test.cpp
  )
  target_compile_features(molshredder_mrc_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_mrc_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_mrc_reader_tests)
  add_test(
    NAME io.mrc_reader
    COMMAND molshredder_mrc_reader_tests
      "${MOLSHREDDER_MRC_FIXTURE}"
      "${MOLSHREDDER_MRC_BIG_FIXTURE}"
  )

  add_executable(
    molshredder_mrc_writer_tests
    tests/io/mrc_writer_test.cpp
  )
  target_compile_features(molshredder_mrc_writer_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_mrc_writer_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_mrc_writer_tests)
  add_test(
    NAME io.mrc_writer
    COMMAND molshredder_mrc_writer_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(io.mrc_reader PROPERTIES FIXTURES_REQUIRED mrc_fixture)

  add_executable(
    molshredder_structure_reader_tests
    tests/io/structure_reader_test.cpp
  )

  foreach(
    MOLSHREDDER_MOLFILE_TEST_VARIANT
    IN ITEMS valid alternate incompatible duplicate missing_entry wrong_type
  )
    add_library(
      molshredder_molfile_test_${MOLSHREDDER_MOLFILE_TEST_VARIANT}
      SHARED tests/support/molfile_test_plugin.cpp
    )
    target_compile_features(
      molshredder_molfile_test_${MOLSHREDDER_MOLFILE_TEST_VARIANT}
      PRIVATE cxx_std_20
    )
    target_include_directories(
      molshredder_molfile_test_${MOLSHREDDER_MOLFILE_TEST_VARIANT}
      PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    )
    set_target_properties(
      molshredder_molfile_test_${MOLSHREDDER_MOLFILE_TEST_VARIANT}
      PROPERTIES CXX_EXTENSIONS OFF
    )
    molshredder_enable_warnings(
      molshredder_molfile_test_${MOLSHREDDER_MOLFILE_TEST_VARIANT}
    )
  endforeach()
  target_compile_definitions(
    molshredder_molfile_test_alternate
    PRIVATE MOLSHREDDER_TEST_NAME="pqr_alt" MOLSHREDDER_TEST_SLOW_INIT=1
  )
  target_compile_definitions(
    molshredder_molfile_test_incompatible PRIVATE MOLSHREDDER_TEST_ABI=17
  )
  target_compile_definitions(
    molshredder_molfile_test_duplicate PRIVATE MOLSHREDDER_TEST_DUPLICATE=1
  )
  target_compile_definitions(
    molshredder_molfile_test_missing_entry
    PRIVATE MOLSHREDDER_TEST_OMIT_FINI=1
  )
  target_compile_definitions(
    molshredder_molfile_test_wrong_type
    PRIVATE MOLSHREDDER_TEST_WRONG_TYPE=1
  )

  add_executable(
    molshredder_molfile_provider_tests
    tests/io/molfile_provider_test.cpp
  )
  target_compile_features(
    molshredder_molfile_provider_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_molfile_provider_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_molfile_provider_tests)
  add_test(
    NAME io.molfile_provider
    COMMAND
      molshredder_molfile_provider_tests
      $<TARGET_FILE:molshredder_molfile_test_valid>
      $<TARGET_FILE:molshredder_molfile_test_alternate>
      $<TARGET_FILE:molshredder_molfile_test_incompatible>
      $<TARGET_FILE:molshredder_molfile_test_duplicate>
      $<TARGET_FILE:molshredder_molfile_test_missing_entry>
      $<TARGET_FILE:molshredder_molfile_test_wrong_type>
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_format_capability_tests
    tests/io/format_capabilities_test.cpp
  )
  target_compile_features(
    molshredder_format_capability_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_format_capability_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_format_capability_tests)
  add_test(
    NAME io.format_capabilities
    COMMAND molshredder_format_capability_tests
  )
  target_compile_features(
    molshredder_structure_reader_tests
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_structure_reader_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_structure_reader_tests)
  add_test(
    NAME io.structure_reader
    COMMAND
      molshredder_structure_reader_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.cif"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/1ubq.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.sdf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.mol2"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.gro"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.psf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
  )

  add_executable(molshredder_vtf_reader_tests tests/io/vtf_reader_test.cpp)
  target_compile_features(molshredder_vtf_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_vtf_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_vtf_reader_tests)
  add_test(NAME io.vtf_reader COMMAND molshredder_vtf_reader_tests)

  add_executable(
    molshredder_structure_writer_tests
    tests/io/structure_writer_test.cpp
  )
  target_compile_features(
    molshredder_structure_writer_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_structure_writer_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_structure_writer_tests)
  add_test(
    NAME io.structure_writer
    COMMAND
      molshredder_structure_writer_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.cif"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.sdf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.mol2"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.gro"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.psf"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_dcd_reader_tests
    tests/io/dcd_reader_test.cpp
  )
  target_compile_features(molshredder_dcd_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_dcd_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_dcd_reader_tests)
  add_test(
    NAME io.dcd_reader
    COMMAND molshredder_dcd_reader_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_trr_reader_tests
    tests/io/trr_reader_test.cpp
  )
  target_compile_features(molshredder_trr_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_trr_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_trr_reader_tests)
  add_test(
    NAME io.trr_reader
    COMMAND molshredder_trr_reader_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(io.trr_reader PROPERTIES FIXTURES_SETUP trr_fixture)

  add_executable(
    molshredder_xtc_reader_tests
    tests/io/xtc_reader_test.cpp
  )
  target_compile_features(molshredder_xtc_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_xtc_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_xtc_reader_tests)
  add_test(
    NAME io.xtc_reader
    COMMAND molshredder_xtc_reader_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_rst7_reader_tests
    tests/io/rst7_reader_test.cpp
  )
  target_compile_features(molshredder_rst7_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_rst7_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_rst7_reader_tests)
  add_test(
    NAME io.rst7_reader
    COMMAND molshredder_rst7_reader_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/frame.rst7"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_amber_ascii_reader_tests
    tests/io/amber_ascii_reader_test.cpp
  )
  target_compile_features(
    molshredder_amber_ascii_reader_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_amber_ascii_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_amber_ascii_reader_tests)
  add_test(
    NAME io.amber_ascii_reader
    COMMAND
      molshredder_amber_ascii_reader_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.mdcrd"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_binpos_reader_tests
    tests/io/binpos_reader_test.cpp
  )
  target_compile_features(
    molshredder_binpos_reader_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_binpos_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_binpos_reader_tests)
  add_test(
    NAME io.binpos_reader
    COMMAND
      molshredder_binpos_reader_tests
      "${MOLSHREDDER_BINPOS_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    io.binpos_reader PROPERTIES FIXTURES_REQUIRED binpos_fixture
  )

  add_executable(
    molshredder_amber_netcdf_reader_tests
    tests/io/amber_netcdf_reader_test.cpp
  )
  target_compile_features(
    molshredder_amber_netcdf_reader_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_amber_netcdf_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_amber_netcdf_reader_tests)
  add_test(
    NAME io.amber_netcdf_reader
    COMMAND molshredder_amber_netcdf_reader_tests
      "${MOLSHREDDER_AMBER_NETCDF_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_COMPRESSED_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_BAD_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_PARTIAL_CELL_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_MISSING_DATA_FIXTURE}"
      "${MOLSHREDDER_AMBER_NETCDF_RESTART_FIXTURE}"
  )
  set_tests_properties(
    io.amber_netcdf_reader PROPERTIES FIXTURES_REQUIRED amber_netcdf_fixture
  )

  add_executable(
    molshredder_bcif_reader_tests tests/io/bcif_reader_test.cpp
  )
  target_compile_features(molshredder_bcif_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_bcif_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_bcif_reader_tests)
  add_test(
    NAME io.bcif_reader
    COMMAND molshredder_bcif_reader_tests
      "${MOLSHREDDER_BCIF_FIXTURE}"
      "${MOLSHREDDER_BCIF_OLD_FIXTURE}"
  )
  set_tests_properties(io.bcif_reader PROPERTIES FIXTURES_REQUIRED bcif_fixture)

  add_executable(molshredder_h5md_reader_tests tests/io/h5md_reader_test.cpp)
  target_compile_features(molshredder_h5md_reader_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_h5md_reader_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_h5md_reader_tests)
  add_test(
    NAME io.h5md_reader
    COMMAND molshredder_h5md_reader_tests
      "${MOLSHREDDER_H5MD_FIXTURE}"
      "${MOLSHREDDER_H5MD_FIXED_FIXTURE}"
      "${MOLSHREDDER_H5MD_AMBIGUOUS_FIXTURE}"
      "${MOLSHREDDER_H5MD_PARTIAL_FIXTURE}"
      "${MOLSHREDDER_H5MD_MISSING_FIXTURE}"
      "${MOLSHREDDER_H5MD_DYNAMIC_ID_FIXTURE}"
  )
  set_tests_properties(io.h5md_reader PROPERTIES FIXTURES_REQUIRED h5md_fixture)

  add_executable(
    molshredder_lammps_dump_reader_tests
    tests/io/lammps_dump_reader_test.cpp
  )
  target_compile_features(
    molshredder_lammps_dump_reader_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_lammps_dump_reader_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_lammps_dump_reader_tests)
  add_test(
    NAME io.lammps_dump_reader
    COMMAND
      molshredder_lammps_dump_reader_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.lammpstrj"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(molshredder_scene_tests tests/scene/scene_test.cpp)
  target_compile_features(molshredder_scene_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_scene_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_scene_tests)
  add_test(NAME scene.hierarchy COMMAND molshredder_scene_tests)

  add_executable(molshredder_camera_tests tests/scene/camera_test.cpp)
  target_compile_features(molshredder_camera_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_camera_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_camera_tests)
  add_test(NAME scene.camera COMMAND molshredder_camera_tests)

  add_executable(molshredder_stereo_tests tests/scene/stereo_test.cpp)
  target_compile_features(molshredder_stereo_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_stereo_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_stereo_tests)
  add_test(NAME scene.stereo COMMAND molshredder_stereo_tests)

  add_executable(
    molshredder_pymol_view_tests
    tests/scene/pymol_view_test.cpp
  )
  target_compile_features(molshredder_pymol_view_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_pymol_view_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_pymol_view_tests)
  add_test(NAME scene.pymol_view COMMAND molshredder_pymol_view_tests)

  add_executable(
    molshredder_representation_tests
    tests/render/representation_test.cpp
  )
  target_compile_features(molshredder_representation_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_representation_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_representation_tests)
  add_test(NAME render.representation COMMAND molshredder_representation_tests)

  add_executable(
    molshredder_p0_geometry_tests
    tests/render/p0_geometry_test.cpp
  )
  target_compile_features(molshredder_p0_geometry_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_p0_geometry_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_p0_geometry_tests)
  add_test(NAME render.p0_geometry COMMAND molshredder_p0_geometry_tests)

  add_executable(
    molshredder_render_setting_store_tests
    tests/render/setting_store_test.cpp
  )
  target_compile_features(
    molshredder_render_setting_store_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_render_setting_store_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_render_setting_store_tests)
  add_test(
    NAME render.setting_store
    COMMAND molshredder_render_setting_store_tests
  )

  add_executable(
    molshredder_volume_isosurface_tests
    tests/render/volume_isosurface_test.cpp
  )
  target_compile_features(
    molshredder_volume_isosurface_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_volume_isosurface_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_volume_isosurface_tests)
  add_test(
    NAME render.volume_isosurface
    COMMAND molshredder_volume_isosurface_tests
  )

  add_executable(
    molshredder_representation_scaling_tests
    tests/render/representation_scaling_test.cpp
  )
  target_compile_features(
    molshredder_representation_scaling_tests
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_representation_scaling_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_representation_scaling_tests)
  add_test(
    NAME render.representation_scaling
    COMMAND molshredder_representation_scaling_tests
  )

  add_executable(
    molshredder_reference_renderer_tests
    tests/render/reference_renderer_test.cpp
  )
  target_compile_features(
    molshredder_reference_renderer_tests
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_reference_renderer_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_reference_renderer_tests)
  add_test(
    NAME render.reference
    COMMAND molshredder_reference_renderer_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_selection_tests
    tests/selection/selection_test.cpp
  )
  target_compile_features(molshredder_selection_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_selection_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_selection_tests)
  add_test(NAME selection.expression COMMAND molshredder_selection_tests)

  add_executable(
    molshredder_basic_analysis_tests
    tests/analysis/basic_test.cpp
  )
  target_compile_features(molshredder_basic_analysis_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_basic_analysis_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_basic_analysis_tests)
  add_test(NAME analysis.basic COMMAND molshredder_basic_analysis_tests)

  add_executable(
    molshredder_alignment_tests
    tests/analysis/alignment_test.cpp
  )
  target_compile_features(molshredder_alignment_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_alignment_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_alignment_tests)
  add_test(NAME analysis.alignment COMMAND molshredder_alignment_tests)

  add_executable(
    molshredder_principal_axes_tests
    tests/analysis/principal_axes_test.cpp
  )
  target_compile_features(molshredder_principal_axes_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_principal_axes_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_principal_axes_tests)
  add_test(
    NAME analysis.principal_axes
    COMMAND molshredder_principal_axes_tests
  )

  add_executable(
    molshredder_time_series_tests
    tests/analysis/time_series_test.cpp
  )
  target_compile_features(molshredder_time_series_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_time_series_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_time_series_tests)
  add_test(NAME analysis.time_series COMMAND molshredder_time_series_tests)

  add_executable(molshredder_contacts_tests tests/analysis/contacts_test.cpp)
  target_compile_features(molshredder_contacts_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_contacts_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_contacts_tests)
  add_test(NAME analysis.contacts COMMAND molshredder_contacts_tests)

  add_executable(molshredder_secondary_structure_tests
                 tests/analysis/secondary_structure_test.cpp)
  target_compile_features(molshredder_secondary_structure_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_secondary_structure_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_secondary_structure_tests)
  add_test(NAME analysis.secondary_structure COMMAND molshredder_secondary_structure_tests)

  add_executable(
    molshredder_frame_cache_tests
    tests/trajectory/frame_cache_test.cpp
  )
  target_compile_features(molshredder_frame_cache_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_frame_cache_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_frame_cache_tests)
  add_test(NAME trajectory.frame_cache COMMAND molshredder_frame_cache_tests)

  add_executable(
    molshredder_trajectory_attachment_tests
    tests/trajectory/attachment_test.cpp
  )
  target_compile_features(
    molshredder_trajectory_attachment_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_trajectory_attachment_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_trajectory_attachment_tests)
  add_test(
    NAME trajectory.attachment
    COMMAND molshredder_trajectory_attachment_tests
  )

  add_executable(
    molshredder_prefetch_scheduler_tests
    tests/trajectory/prefetch_scheduler_test.cpp
  )
  target_compile_features(
    molshredder_prefetch_scheduler_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_prefetch_scheduler_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_prefetch_scheduler_tests)
  add_test(
    NAME trajectory.prefetch_scheduler
    COMMAND molshredder_prefetch_scheduler_tests
  )

  add_executable(
    molshredder_playback_tests
    tests/trajectory/playback_test.cpp
  )
  target_compile_features(molshredder_playback_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_playback_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_playback_tests)
  add_test(NAME trajectory.playback COMMAND molshredder_playback_tests)

  add_executable(
    molshredder_pbc_tests
    tests/trajectory/pbc_test.cpp
  )
  target_compile_features(molshredder_pbc_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_pbc_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_pbc_tests)
  add_test(NAME trajectory.pbc COMMAND molshredder_pbc_tests)

  add_executable(
    molshredder_representation_visibility_tests
    tests/application/representation_visibility_test.cpp
  )
  target_compile_features(
    molshredder_representation_visibility_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_representation_visibility_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_representation_visibility_tests)
  add_test(
    NAME application.representation_visibility
    COMMAND molshredder_representation_visibility_tests
  )

  add_executable(
    molshredder_analysis_result_tests
    tests/application/analysis_result_test.cpp
  )
  target_compile_features(molshredder_analysis_result_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_analysis_result_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_analysis_result_tests)
  add_test(
    NAME application.analysis_result
    COMMAND molshredder_analysis_result_tests "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_executable(
    molshredder_workspace_tests
    tests/application/workspace_test.cpp
  )
  target_compile_features(molshredder_workspace_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_workspace_tests
    PRIVATE MolShredder::Gui MolShredder::Cli
  )
  molshredder_enable_warnings(molshredder_workspace_tests)
  add_test(
    NAME application.workspace
    COMMAND
      molshredder_workspace_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/application/fixtures/pbc_distance.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/application/fixtures/hydrogen_bond.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.sdf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.mol2"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.gro"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.psf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/frame.rst7"
  )

  add_executable(
    molshredder_runtime_diagnostics_tests
    tests/application/runtime_diagnostics_test.cpp
  )
  target_compile_features(
    molshredder_runtime_diagnostics_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_runtime_diagnostics_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_runtime_diagnostics_tests)
  add_test(
    NAME application.runtime_diagnostics
    COMMAND molshredder_runtime_diagnostics_tests
  )

  add_executable(
    molshredder_view_state_tests
    tests/application/view_state_test.cpp
  )
  target_compile_features(molshredder_view_state_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_view_state_tests PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_view_state_tests)
  add_test(NAME application.view_state COMMAND molshredder_view_state_tests)

  add_executable(
    molshredder_volume_workspace_tests
    tests/application/volume_workspace_test.cpp
  )
  target_compile_features(
    molshredder_volume_workspace_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_volume_workspace_tests PRIVATE MolShredder::Gui
  )
  molshredder_enable_warnings(molshredder_volume_workspace_tests)
  add_test(
    NAME application.volume_workspace
    COMMAND molshredder_volume_workspace_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/potential.dx"
            "${MOLSHREDDER_MRC_FIXTURE}"
  )
  set_tests_properties(
    application.volume_workspace PROPERTIES FIXTURES_REQUIRED mrc_fixture
  )

  add_executable(
    molshredder_trajectory_controller_tests
    tests/application/trajectory_controller_test.cpp
  )
  target_compile_features(
    molshredder_trajectory_controller_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_trajectory_controller_tests
    PRIVATE MolShredder::Gui
  )
  molshredder_enable_warnings(molshredder_trajectory_controller_tests)
  add_test(
    NAME application.trajectory_controller
    COMMAND
      molshredder_trajectory_controller_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
      "${CMAKE_CURRENT_BINARY_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/application/fixtures/hydrogen_bond.pdb"
  )

  add_executable(
    molshredder_session_tests
    tests/application/session_test.cpp
  )
  target_compile_features(molshredder_session_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_session_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_session_tests)
  add_test(
    NAME application.session
    COMMAND
      molshredder_session_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_executable(
    molshredder_gui_action_tests
    tests/gui/action_adapter_test.cpp
  )
  target_compile_features(molshredder_gui_action_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_gui_action_tests PRIVATE MolShredder::Gui)
  molshredder_enable_warnings(molshredder_gui_action_tests)
  add_test(NAME gui.action_adapter COMMAND molshredder_gui_action_tests)

  add_executable(
    molshredder_gui_analysis_presenter_tests
    tests/gui/analysis_presenter_test.cpp
  )
  target_compile_features(
    molshredder_gui_analysis_presenter_tests PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_gui_analysis_presenter_tests PRIVATE MolShredder::Gui
  )
  molshredder_enable_warnings(molshredder_gui_analysis_presenter_tests)
  add_test(
    NAME gui.analysis_presenter
    COMMAND
      molshredder_gui_analysis_presenter_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_executable(molshredder_command_tests tests/command/registry_test.cpp)
  target_compile_features(molshredder_command_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_command_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_command_tests)
  add_test(NAME command.registry COMMAND molshredder_command_tests)

  add_executable(
    molshredder_serialization_tests
    tests/command/serialization_test.cpp
  )
  target_compile_features(molshredder_serialization_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_serialization_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_serialization_tests)
  add_test(NAME command.serialization COMMAND molshredder_serialization_tests)

  add_executable(molshredder_result_tests tests/command/result_test.cpp)
  target_compile_features(molshredder_result_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_result_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_result_tests)
  add_test(
    NAME command.result
    COMMAND
      molshredder_result_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/command/fixtures/result_success.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/command/fixtures/result_error.json"
  )

  add_executable(molshredder_alias_tests tests/command/alias_test.cpp)
  target_compile_features(molshredder_alias_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_alias_tests PRIVATE MolShredder::Core)
  molshredder_enable_warnings(molshredder_alias_tests)
  add_test(
    NAME command.alias
    COMMAND
      molshredder_alias_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/command/fixtures/alias_expansion.golden"
  )

  add_executable(
    molshredder_foundation_grammar_tests
    tests/command/foundation_grammar_test.cpp
  )
  target_compile_features(
    molshredder_foundation_grammar_tests
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    molshredder_foundation_grammar_tests
    PRIVATE MolShredder::Core
  )
  molshredder_enable_warnings(molshredder_foundation_grammar_tests)
  add_test(
    NAME command.foundation_grammar
    COMMAND
      molshredder_foundation_grammar_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/command/fixtures/foundation_grammar.golden"
  )

  add_executable(molshredder_cli_tests tests/cli/adapter_test.cpp)
  target_compile_features(molshredder_cli_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_cli_tests PRIVATE MolShredder::Cli)
  molshredder_enable_warnings(molshredder_cli_tests)
  add_test(NAME cli.adapter COMMAND molshredder_cli_tests)

  add_executable(
    molshredder_foundation_cli_tests
    tests/cli/foundation_grammar_test.cpp
  )
  target_compile_features(molshredder_foundation_cli_tests PRIVATE cxx_std_20)
  target_link_libraries(
    molshredder_foundation_cli_tests
    PRIVATE MolShredder::Cli
  )
  molshredder_enable_warnings(molshredder_foundation_cli_tests)
  add_test(NAME cli.foundation_grammar COMMAND molshredder_foundation_cli_tests)

  add_executable(molshredder_console_tests tests/cli/console_test.cpp)
  target_compile_features(molshredder_console_tests PRIVATE cxx_std_20)
  target_link_libraries(molshredder_console_tests PRIVATE MolShredder::Cli)
  molshredder_enable_warnings(molshredder_console_tests)
  add_test(NAME cli.console COMMAND molshredder_console_tests)

  add_executable(molshredder_gui_probe tests/frontend/gui_probe.cpp)
  target_compile_features(molshredder_gui_probe PRIVATE cxx_std_20)
  target_link_libraries(molshredder_gui_probe PRIVATE MolShredder::Gui)
  molshredder_enable_warnings(molshredder_gui_probe)
  add_test(
    NAME frontend.parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/parity_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
  )

  add_test(
    NAME frontend.representation_visibility_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/representation_visibility_parity_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
  )

  add_test(
    NAME frontend.render_setting_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/render_setting_parity_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
  )

  add_test(
    NAME frontend.python_script_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/python_script_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/automation/fixtures/success.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/automation/fixtures/failure_after_mutation.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_test(
    NAME frontend.system_info_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/system_info_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${MOLSHREDDER_SUPPORT_GENERATED_DIR}/molshredder-support.json"
  )

  add_test(
    NAME frontend.console_script_portability
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/console_script_test.py"
  )

  add_executable(
    molshredder_analysis_probe
    tests/frontend/analysis_probe.cpp
  )
  target_compile_features(molshredder_analysis_probe PRIVATE cxx_std_20)
  target_link_libraries(molshredder_analysis_probe PRIVATE MolShredder::Gui)
  molshredder_enable_warnings(molshredder_analysis_probe)
  add_test(
    NAME frontend.analysis_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/analysis_parity_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_analysis_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_test(
    NAME frontend.object_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/object_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_test(
    NAME frontend.batch_load_parity
    COMMAND
      ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/batch_load_parity_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_gui_probe>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_test(
    NAME frontend.molfile_provider_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/molfile_provider_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "$<TARGET_FILE:molshredder_molfile_test_valid>"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.save_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/save_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.cif"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/multiframe.xyz"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/electrostatics.pqr"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.sdf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/chemistry.mol2"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.gro"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.g96"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.psf"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/frame.rst7"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.mdcrd_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/mdcrd_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.mdcrd"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.rst7_writer_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/rst7_writer_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/frame.rst7"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.trr_writer_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/trr_writer_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_BINARY_DIR}/synthetic_float.trr"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    frontend.trr_writer_parity PROPERTIES FIXTURES_REQUIRED trr_fixture
  )

  add_test(
    NAME frontend.dcd_writer_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/dcd_writer_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/multiframe.xyz"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.amber_netcdf_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/amber_netcdf_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/topology.prmtop"
      "${MOLSHREDDER_AMBER_NETCDF_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    frontend.amber_netcdf_parity
    PROPERTIES FIXTURES_REQUIRED amber_netcdf_fixture
  )

  add_test(
    NAME frontend.h5md_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/h5md_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
      "${MOLSHREDDER_H5MD_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    frontend.h5md_parity PROPERTIES FIXTURES_REQUIRED h5md_fixture
  )

  add_test(
    NAME frontend.bcif_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/bcif_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${MOLSHREDDER_BCIF_FIXTURE}"
  )
  set_tests_properties(
    frontend.bcif_parity PROPERTIES FIXTURES_REQUIRED bcif_fixture
  )

  add_test(
    NAME frontend.opendx_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/opendx_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/potential.dx"
  )

  add_test(
    NAME frontend.mrc_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/mrc_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${MOLSHREDDER_MRC_FIXTURE}"
  )
  set_tests_properties(
    frontend.mrc_parity PROPERTIES FIXTURES_REQUIRED mrc_fixture
  )

  add_test(
    NAME frontend.volume_isosurface_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/volume_isosurface_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${MOLSHREDDER_MRC_FIXTURE}"
  )
  set_tests_properties(
    frontend.volume_isosurface_parity
    PROPERTIES FIXTURES_REQUIRED mrc_fixture
  )

  add_test(
    NAME frontend.vtf_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/vtf_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/coarse_grained.vtf"
  )

  add_test(
    NAME frontend.lammps_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/lammps_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/trajectory.lammpstrj"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )

  add_test(
    NAME frontend.binpos_parity
    COMMAND
      Python3::Interpreter
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend/binpos_frontend_test.py"
      "$<TARGET_FILE:molshredder_python>"
      "$<TARGET_FILE:molshredder>"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/lammps_topology.pdb"
      "${MOLSHREDDER_BINPOS_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
  set_tests_properties(
    frontend.binpos_parity PROPERTIES FIXTURES_REQUIRED binpos_fixture
  )

  add_test(NAME cli.version COMMAND molshredder --version)
  add_test(NAME cli.help COMMAND molshredder --help)
  add_test(NAME cli.system_version COMMAND molshredder system version)
  add_test(NAME cli.system_info COMMAND molshredder system info --format json)
  add_test(NAME cli.version_alias COMMAND molshredder version)
  add_test(
    NAME cli.version_json
    COMMAND molshredder system version --format json
  )
  add_test(
    NAME cli.foundation_help
    COMMAND molshredder analyze center --help
  )
  add_test(NAME cli.invalid COMMAND molshredder unsupported)
  set_tests_properties(
    cli.version
    PROPERTIES PASS_REGULAR_EXPRESSION "MolShredder ${PROJECT_VERSION}"
  )
  set_tests_properties(
    cli.help
    PROPERTIES PASS_REGULAR_EXPRESSION "molshredder \\[OPTIONS\\]"
  )
  set_tests_properties(
    cli.system_version
    PROPERTIES PASS_REGULAR_EXPRESSION "MolShredder ${PROJECT_VERSION}"
  )
  set_tests_properties(
    cli.system_info
    PROPERTIES
      PASS_REGULAR_EXPRESSION
        "\"configuration_schema_version\":1"
  )
  set_tests_properties(
    cli.version_alias
    PROPERTIES PASS_REGULAR_EXPRESSION "MolShredder ${PROJECT_VERSION}"
  )
  set_tests_properties(
    cli.version_json
    PROPERTIES
      PASS_REGULAR_EXPRESSION
        "\\\"status\\\":\\\"ok\\\".*\\\"command\\\":\\\"invoke \\\\\\\"system version\\\\\\\"\\\""
  )
  set_tests_properties(
    cli.foundation_help
    PROPERTIES PASS_REGULAR_EXPRESSION "choices: centroid, com"
  )
  set_tests_properties(cli.invalid PROPERTIES WILL_FAIL TRUE)
endif()
