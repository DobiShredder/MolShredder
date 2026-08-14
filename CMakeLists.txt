cmake_minimum_required(VERSION 3.25)

project(
  MolShredder
  VERSION 0.1.0
  DESCRIPTION "High-performance molecular visualization and analysis"
  LANGUAGES CXX
)

find_package(Threads REQUIRED)

include(CTest)
include(FetchContent)
include(GNUInstallDirs)

option(MOLSHREDDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MOLSHREDDER_BUILD_DESKTOP "Build the Qt Quick desktop prototype" OFF)

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

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
set(PYBIND11_FINDPYTHON ON)
set(PYBIND11_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v3.1.0.tar.gz
  URL_HASH SHA256=ef712655692a2e9bf7bb7874c022564a45f91d847ddee987e720cd9e28849665
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pybind11)

function(molshredder_enable_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
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

add_library(molshredder_core STATIC)
add_library(MolShredder::Core ALIAS molshredder_core)

target_sources(
  molshredder_core
  PRIVATE
    src/analysis/alignment.cpp
    src/analysis/basic.cpp
    src/analysis/contacts.cpp
    src/analysis/secondary_structure.cpp
    src/analysis/time_series.cpp
    src/application/default_registry.cpp
    src/application/dispatcher.cpp
    src/application/session.cpp
    src/application/workspace.cpp
    src/command/foundation_grammar.cpp
    src/command/registry.cpp
    src/command/result.cpp
    src/command/serialization.cpp
    src/core/version.cpp
    src/io/mmcif_reader.cpp
    src/io/dcd_reader.cpp
    src/io/trr_reader.cpp
    src/io/xtc_reader.cpp
    src/io/pdb_reader.cpp
    src/io/structure_reader.cpp
    src/io/trajectory_reader.cpp
    src/model/coordinates.cpp
    src/model/molecular_system.cpp
    src/model/topology.cpp
    src/render/reference_renderer.cpp
    src/render/representation.cpp
    src/selection/evaluator.cpp
    src/selection/expression.cpp
    src/selection/named_selection.cpp
    src/trajectory/frame_cache.cpp
    src/trajectory/prefetch_scheduler.cpp
    src/trajectory/playback.cpp
    src/trajectory/pbc.cpp
    src/scene/camera.cpp
    src/scene/math.cpp
    src/scene/scene.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/molshredder/analysis/alignment.hpp
      include/molshredder/analysis/basic.hpp
      include/molshredder/analysis/contacts.hpp
      include/molshredder/analysis/secondary_structure.hpp
      include/molshredder/analysis/time_series.hpp
      include/molshredder/application/default_registry.hpp
      include/molshredder/application/dispatcher.hpp
      include/molshredder/application/session.hpp
      include/molshredder/application/workspace.hpp
      include/molshredder/command/foundation_grammar.hpp
      include/molshredder/operation/common_types.hpp
      include/molshredder/operation/error.hpp
      include/molshredder/operation/operation.hpp
      include/molshredder/operation/result.hpp
      include/molshredder/operation/task_context.hpp
      include/molshredder/command/registry.hpp
      include/molshredder/command/result.hpp
      include/molshredder/command/serialization.hpp
      include/molshredder/io/structure_reader.hpp
      include/molshredder/io/trajectory_reader.hpp
      include/molshredder/model/coordinates.hpp
      include/molshredder/model/molecular_system.hpp
      include/molshredder/model/topology.hpp
      include/molshredder/render/packet.hpp
      include/molshredder/render/reference_renderer.hpp
      include/molshredder/render/representation.hpp
      include/molshredder/selection/evaluator.hpp
      include/molshredder/selection/expression.hpp
      include/molshredder/selection/named_selection.hpp
      include/molshredder/trajectory/frame_cache.hpp
      include/molshredder/trajectory/prefetch_scheduler.hpp
      include/molshredder/trajectory/playback.hpp
      include/molshredder/trajectory/pbc.hpp
      include/molshredder/scene/camera.hpp
      include/molshredder/scene/math.hpp
      include/molshredder/scene/scene.hpp
      include/molshredder/version.hpp
)

target_compile_features(molshredder_core PUBLIC cxx_std_20)
target_link_libraries(molshredder_core PUBLIC Threads::Threads)
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
target_link_libraries(molshredder PRIVATE MolShredder::Cli)
molshredder_enable_warnings(molshredder)

pybind11_add_module(molshredder_python MODULE src/python/module.cpp)
set_target_properties(
  molshredder_python
  PROPERTIES
    OUTPUT_NAME molshredder
    CXX_EXTENSIONS OFF
)
target_compile_features(molshredder_python PRIVATE cxx_std_20)
target_link_libraries(molshredder_python PRIVATE MolShredder::Core)
molshredder_enable_warnings(molshredder_python)

if(MOLSHREDDER_BUILD_DESKTOP)
  find_package(
    Qt6 6.8.3 EXACT REQUIRED
    COMPONENTS Core Gui Qml Quick ShaderTools
  )
  qt_standard_project_setup(REQUIRES 6.8)
  qt_add_executable(
    molshredder_desktop
    MACOSX_BUNDLE
    WIN32
    apps/desktop/main.cpp
    apps/desktop/viewport_item.cpp
    apps/desktop/viewport_item.hpp
  )
  target_compile_features(molshredder_desktop PRIVATE cxx_std_20)
  target_include_directories(
    molshredder_desktop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/desktop"
  )
  target_link_libraries(
    molshredder_desktop
    PRIVATE
      MolShredder::Gui
      Qt6::Core
      Qt6::Gui
      Qt6::GuiPrivate
      Qt6::Qml
      Qt6::Quick
  )
  molshredder_enable_warnings(molshredder_desktop)
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
  )
  install(
    TARGETS molshredder_desktop
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
  if(BUILD_TESTING)
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
    add_test(NAME desktop.gpu_smoke COMMAND molshredder_desktop --smoke)
    set_tests_properties(
      desktop.gpu_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder GPU primitives ready: mesh=[1-9][0-9]* lines=[1-9][0-9]* cylinders=[1-9][0-9]* spheres=[1-9][0-9]*"
    )
    add_test(
      NAME desktop.structure_open_smoke
      COMMAND
        molshredder_desktop --smoke
        "--camera-smoke"
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
        "--representation=sticks"
    )
    set_tests_properties(
      desktop.structure_open_smoke
      PROPERTIES
        TIMEOUT 10
        PASS_REGULAR_EXPRESSION "MolShredder desktop camera ready: representation=sticks atoms=3"
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
      NAME desktop.gpu_picking_smoke
      COMMAND
        molshredder_desktop --picking-smoke
        "--open=${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/pick_single_atom.pdb"
        "--representation=spheres"
    )
    set_tests_properties(
      desktop.gpu_picking_smoke
      PROPERTIES
        TIMEOUT 10
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
        PASS_REGULAR_EXPRESSION "MolShredder desktop objects ready: objects=2 active=1 visible=1 spheres=1"
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
  endif()
endif()

install(
  TARGETS molshredder molshredder_core molshredder_gui molshredder_cli
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
  FILES docs/schemas/result-envelope-v1.schema.json
  DESTINATION "${CMAKE_INSTALL_DATADIR}/molshredder/schemas"
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

  add_executable(
    molshredder_structure_reader_tests
    tests/io/structure_reader_test.cpp
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
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/io/fixtures/synthetic_multimodel.pdb"
  )

  add_test(NAME cli.version COMMAND molshredder --version)
  add_test(NAME cli.help COMMAND molshredder --help)
  add_test(NAME cli.system_version COMMAND molshredder system version)
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
