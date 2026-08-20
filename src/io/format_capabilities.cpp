#include "molshredder/io/format_capabilities.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "molshredder/version.hpp"

namespace molshredder::io {
namespace {

FormatProvider native_provider() {
  return FormatProvider{"native",
                        std::string{molshredder::version()},
                        FormatProviderOrigin::native_builtin,
                        FormatProviderTrust::trusted_builtin,
                        FormatProviderLicenseStatus::approved,
                        "GPL-3.0-or-later",
                        true,
                        {}};
}

std::string unavailable_reason(bool available, FormatDirection direction) {
  if (available) return {};
  return "no registered " + std::string{to_string(direction)} +
         " implementation for this format";
}

int provider_rank(const FormatProvider &provider) {
  const auto trust = provider.trust == FormatProviderTrust::trusted_builtin
                         ? 0
                         : provider.trust ==
                                   FormatProviderTrust::trusted_configured
                               ? 1
                               : 2;
  const auto origin = provider.origin == FormatProviderOrigin::native_builtin
                          ? 0
                          : provider.origin ==
                                    FormatProviderOrigin::dynamic_plugin
                                ? 1
                                : 2;
  return trust * 10 + origin;
}

bool qualified(const FormatCapability &capability,
               FormatDirection direction) {
  const auto *entry = direction_capability(capability, direction);
  return entry != nullptr && entry->available && capability.provider.available &&
         capability.provider.license_status ==
             FormatProviderLicenseStatus::approved;
}

} // namespace

FormatCapability migrate_format_capability_v2(
    FormatCapabilityV2 capability,
    std::map<std::string, std::string, std::less<>> unknown_fields) {
  const auto read_reason =
      unavailable_reason(capability.readable, FormatDirection::read);
  const auto write_reason =
      unavailable_reason(capability.writable, FormatDirection::write);
  std::vector<FormatDirectionCapability> directions;
  directions.reserve(2U);
  directions.push_back(
      {FormatDirection::read,
       capability.readable,
       read_reason,
       capability.readable ? capability.channels : std::vector<std::string>{},
       capability.limitations,
       false});
  directions.push_back(
      {FormatDirection::write,
       capability.writable,
       write_reason,
       capability.writable ? capability.channels : std::vector<std::string>{},
       capability.limitations,
       capability.writable});
  return FormatCapability{
      std::move(capability.id),
      std::move(capability.family),
      std::move(capability.extensions),
      capability.readable,
      capability.writable,
      capability.multi_frame,
      capability.multi_structure,
      capability.random_access,
      capability.streaming,
      std::move(capability.channels),
      std::move(capability.limitations),
      std::move(capability.implementation),
      native_provider(),
      std::move(directions),
      std::move(unknown_fields)};
}

const FormatDirectionCapability *direction_capability(
    const FormatCapability &capability, FormatDirection direction) noexcept {
  const auto found = std::find_if(
      capability.directions.begin(), capability.directions.end(),
      [direction](const auto &item) { return item.direction == direction; });
  return found == capability.directions.end() ? nullptr : &*found;
}

std::string_view to_string(FormatDirection direction) noexcept {
  switch (direction) {
  case FormatDirection::read:
    return "read";
  case FormatDirection::write:
    return "write";
  }
  return "read";
}

std::string_view to_string(FormatProviderOrigin origin) noexcept {
  switch (origin) {
  case FormatProviderOrigin::native_builtin:
    return "native_builtin";
  case FormatProviderOrigin::dynamic_plugin:
    return "dynamic_plugin";
  case FormatProviderOrigin::external_converter:
    return "external_converter";
  }
  return "native_builtin";
}

std::string_view to_string(FormatProviderTrust trust) noexcept {
  switch (trust) {
  case FormatProviderTrust::trusted_builtin:
    return "trusted_builtin";
  case FormatProviderTrust::trusted_configured:
    return "trusted_configured";
  case FormatProviderTrust::untrusted:
    return "untrusted";
  }
  return "untrusted";
}

std::string_view to_string(FormatProviderLicenseStatus status) noexcept {
  switch (status) {
  case FormatProviderLicenseStatus::approved:
    return "approved";
  case FormatProviderLicenseStatus::pending:
    return "pending";
  case FormatProviderLicenseStatus::rejected:
    return "rejected";
  }
  return "rejected";
}

const std::vector<FormatCapability> &format_capabilities() {
  static const std::vector<FormatCapability> capabilities = [] {
    std::vector<FormatCapabilityV2> legacy{
      {"pdb",
       "structure",
       {".pdb", ".ent"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "element", "formal_charge",
        "coordinates", "presence", "occupancy", "b_factor", "unit_cell",
        "connectivity", "source_metadata"},
       {"fixed_column_pdb_3_3", "unknown_bond_order", "constant_unit_cell",
        "semantic_loss_report", "partial_pdb_3_3_records"},
       "native"},
      {"mmcif",
       "structure",
       {".cif", ".mmcif"},
       true,
       true,
       true,
       true,
       true,
       true,
       {"atom_identity", "residue_identity", "element", "formal_charge",
        "coordinates", "presence", "occupancy", "b_factor", "unit_cell",
        "connectivity", "source_metadata"},
       {"partial_pdbx_dictionary", "single_category_loops",
        "constant_unit_cell", "single_active_object_export",
        "semantic_loss_report"},
       "native"},
      {"bcif",
       "structure",
       {".bcif"},
       true,
       false,
       true,
       true,
       true,
       false,
       {"atom_identity", "residue_identity", "element", "formal_charge",
        "coordinates", "presence", "occupancy", "b_factor", "unit_cell",
        "connectivity", "source_metadata", "column_mask"},
       {"read_only", "binarycif_0_3_x", "uncompressed_only",
        "pdbx_structure_subset", "in_memory_messagepack"},
       "native_msgpack"},
      {"opendx",
       "volume",
       {".dx"},
       true,
       true,
       false,
       false,
       true,
       false,
       {"scalar_grid", "dimensions", "origin", "delta_vectors",
        "coordinate_unit", "precision", "value_range", "source_metadata"},
       {"ascii_regular_grid_only", "single_scalar_field",
        "rank_zero_float_or_double", "in_memory_read_write",
        "apbs_default_angstrom", "no_binary_irregular_finite_element"},
       "native"},
      {"mrc",
       "volume",
       {".mrc", ".map", ".ccp4", ".mrcs"},
       true,
       true,
       false,
       false,
       true,
       false,
       {"scalar_grid", "dimensions", "origin", "delta_vectors",
        "coordinate_unit", "precision", "value_range", "axis_permutation",
        "cell_geometry", "extended_header", "source_metadata"},
       {"mrc2014_ccp4", "in_memory_read_write",
        "scalar_modes_0_1_2_6_12", "complex_modes_unsupported",
        "packed_mode_101_unsupported", "skew_transform_unsupported",
        "handedness_unspecified", "single_grid", "mode_2_write",
        "canonical_basis_write", "typed_semantic_loss_report"},
       "native"},
      {"pqr",
       "structure",
       {".pqr"},
       true,
       true,
       false,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "inferred_element", "coordinates",
        "partial_charge", "pqr_radius", "unit_cell"},
       {"single_frame", "whitespace_delimited", "optional_chain_id",
        "no_explicit_element", "cryst1_cell_extension",
        "semantic_loss_report"},
       "native"},
      {"mol",
       "structure",
       {".mol"},
       true,
       true,
       false,
       false,
       true,
       true,
       {"element", "coordinates", "connectivity", "bond_order", "formal_charge",
        "isotope", "radical", "atom_properties", "source_metadata"},
       {"v2000_only", "single_structure", "single_frame",
        "query_bonds_unsupported", "bond_stereo_unsupported",
        "semantic_loss_report"},
       "native"},
      {"sdf",
       "structure",
       {".sdf", ".sd"},
       true,
       true,
       false,
       true,
       true,
       true,
       {"element", "coordinates", "connectivity", "bond_order", "formal_charge",
        "isotope", "radical", "atom_properties", "data_fields",
        "source_metadata"},
       {"v2000_only", "single_frame_per_record", "query_bonds_unsupported",
        "bond_stereo_unsupported", "single_active_object_export",
        "semantic_loss_report"},
       "native"},
      {"mol2",
       "structure",
       {".mol2"},
       true,
       true,
       false,
       true,
       true,
       true,
       {"atom_identity", "residue_identity", "sybyl_atom_type", "coordinates",
        "connectivity", "bond_order", "amide_bond", "partial_charge",
        "charge_presence", "atom_status", "unit_cell", "source_metadata"},
       {"single_frame_per_molecule", "dummy_query_bonds_unsupported",
        "requires_explicit_atom_type_for_write", "single_active_object_export",
        "semantic_loss_report"},
       "native"},
      {"psf",
       "structure",
       {".psf"},
       true,
       true,
       false,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "segment_identity",
        "inferred_element", "force_field_atom_type", "partial_charge", "mass",
        "connectivity", "angles", "dihedrals", "impropers", "term_multiplicity",
        "cmap_cross_terms", "topology_only", "source_metadata"},
       {"no_coordinates", "standard_namd_ext_xplor", "unknown_bond_order",
        "drude_cheq_lone_pair_unsupported", "auxiliary_sections_not_modeled",
        "requires_force_field_properties_for_write", "semantic_loss_report"},
       "native"},
      {"prmtop",
       "structure",
       {".prmtop", ".parm7", ".top"},
       true,
       false,
       false,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "atomic_number",
        "force_field_atom_type", "atom_type_index", "partial_charge", "mass",
        "connectivity", "angles", "dihedrals", "impropers", "term_multiplicity",
        "gb_radius", "gb_screen", "unit_cell_template", "topology_only",
        "source_metadata"},
       {"read_only", "standard_amber_only", "chamber_unsupported",
        "perturbation_unsupported", "force_parameter_arrays_not_modeled",
        "no_coordinates"},
       "native"},
      {"gro",
       "structure",
       {".gro"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "inferred_element", "coordinates",
        "velocity", "physical_time", "unit_cell", "frame_title"},
       {"stable_atom_order", "fixed_width_identity", "five_character_names",
        "no_connectivity", "in_memory_reader", "semantic_loss_report"},
       "native"},
      {"g96",
       "structure",
       {".g96"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "inferred_element", "coordinates",
        "velocity", "source_step", "physical_time", "unit_cell", "frame_blocks",
        "source_metadata"},
       {"stable_atom_order", "fixed_f15_9", "five_character_names",
        "full_or_reduced_blocks", "no_connectivity", "in_memory_reader",
        "semantic_loss_report"},
       "native"},
      {"vtf",
       "structure",
       {".vtf"},
       true,
       false,
       true,
       false,
       true,
       true,
       {"atom_identity", "residue_identity", "atomic_number", "atom_type",
        "partial_charge", "mass", "vdw_radius", "occupancy", "b_factor",
        "connectivity", "coordinates", "unit_cell",
        "sparse_coordinate_inheritance"},
       {"read_only", "vtf_combined_only", "no_vsf_vcf_pairing",
        "unknown_bond_order", "in_memory_reader", "no_timestep_time"},
       "native"},
      {"xyz",
       "structure",
       {".xyz", ".xmol"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"element", "atomic_number", "coordinates", "frame_comment"},
       {"plain_xyz_only", "stable_atom_order", "angstrom_output",
        "semantic_loss_report"},
       "native"},
      {"dcd",
       "trajectory",
       {".dcd"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"coordinates", "source_step", "raw_delta", "unit_cell",
        "precision"},
       {"known_atom_count_required", "single_frame_write",
        "float32_coordinates", "no_typed_physical_time"},
       "native_indexed"},
      {"trr",
       "trajectory",
       {".trr"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"coordinates", "velocity", "force", "source_step", "physical_time",
        "unit_cell", "precision"},
       {"known_atom_count_required", "current_frame_write",
        "write_requires_step_time_lambda", "typed_semantic_loss_report"},
       "native_indexed_xdr"},
      {"xtc",
       "trajectory",
       {".xtc"},
       true,
       false,
       true,
       false,
       true,
       true,
       {"coordinates", "source_step", "physical_time", "unit_cell",
        "compression_precision"},
       {"read_only", "known_atom_count_required", "1995_format_atom_limit"},
       "native_indexed_xdr"},
      {"mdcrd",
       "trajectory",
       {".mdcrd", ".crd", ".crdbox"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"coordinates", "unit_cell", "precision", "frame_title"},
       {"current_frame_crd_or_crdbox_write", "known_atom_count_required", "fixed_width_f8",
        "no_step_or_time", "no_velocity_or_force", "remd_header_unsupported",
        "small_system_box_ambiguity", "crdbox_requires_topology_angle",
        "crdbox_equal_cell_angles_only"},
       "native_indexed_ascii"},
      {"lammps",
       "trajectory",
       {".lammpstrj", ".lammpstraj", ".dump"},
       true,
       false,
       true,
       false,
       true,
       true,
       {"coordinates", "velocity", "source_step", "physical_time",
        "units_style", "unit_cell", "atom_id",
        "frame_atom_properties", "box_origin", "boundary"},
       {"read_only", "known_atom_ids_required", "coordinate_unit_required",
        "orthogonal_or_restricted_triclinic", "general_triclinic_unsupported",
        "one_coordinate_triplet_per_snapshot", "typed_units_real_metal_nano",
        "other_unit_styles_unsupported", "text_dump_only"},
       "native_indexed_ascii"},
      {"netcdf",
       "trajectory",
       {".nc", ".ncdf", ".netcdf", ".ncrst"},
       true,
       false,
       true,
       false,
       true,
       true,
       {"coordinates", "velocity", "force", "physical_time", "temperature",
        "unit_cell", "precision", "scale_factor", "integer_compression",
        "storage_format", "single_frame_restart"},
       {"read_only", "amber_convention_1_0", "amber_restart_convention_1_0",
        "known_atom_count_required",
        "full_3d_unit_cell_only", "remd_metadata_not_exposed",
        "netcdf_c_runtime_dependency"},
       "native_random_access_netcdf_c"},
      {"h5md",
       "trajectory",
       {".h5md"},
       true,
       false,
       true,
       false,
       true,
       true,
       {"coordinates", "velocity", "force", "source_step", "physical_time",
        "unit_cell", "particle_id", "presence", "mass", "charge", "species",
        "image", "precision", "si_units", "particle_group"},
       {"read_only", "h5md_1_x", "three_dimensions_only",
        "topology_source_ids_required_for_non_index_ids",
        "missing_position_unit_requires_override",
        "partial_periodic_boundary_unsupported",
        "arbitrary_observables_not_exposed", "external_storage_rejected",
        "hdf5_runtime_dependency"},
       "native_random_access_hdf5_c"},
      {"binpos",
       "trajectory",
       {".binpos"},
       true,
       true,
       true,
       false,
       true,
       true,
       {"coordinates", "precision", "byte_order", "current_frame_write"},
       {"known_atom_count_required", "float32_coordinates",
        "no_unit_cell", "no_step_or_time", "no_velocity_or_force",
        "native_endian_legacy_read", "little_endian_write",
        "single_frame_export", "uncompressed_only"},
       "native_indexed_binary"},
      {"rst7",
       "trajectory",
       {".rst7", ".restrt", ".inpcrd", ".inprst"},
       true,
       true,
       false,
       false,
       true,
       true,
       {"coordinates", "velocity", "physical_time", "temperature", "unit_cell",
        "source_metadata"},
       {"single_frame", "known_atom_count_required_for_attach",
        "in_memory_read_write", "small_system_optional_block_ambiguity",
        "fixed_f12_7_precision", "semantic_loss_report"},
       "native"},
    };
    std::vector<FormatCapability> values;
    values.reserve(legacy.size());
    for (auto &value : legacy) {
      values.push_back(migrate_format_capability_v2(std::move(value)));
    }
    return values;
  }();
  return capabilities;
}

operation::Result<FormatProvider>
resolve_format_provider(std::string_view format_id, FormatDirection direction,
                        std::string_view requested_provider) {
  if (requested_provider.empty()) requested_provider = "auto";
  const auto &capabilities = format_capabilities();
  std::vector<const FormatCapability *> candidates;
  bool format_registered = format_id.empty();
  bool provider_registered = requested_provider == "auto";
  for (const auto &capability : capabilities) {
    if (requested_provider != "auto" &&
        capability.provider.id == requested_provider) {
      provider_registered = true;
    }
    if (!format_id.empty() && capability.id != format_id) continue;
    format_registered = true;
    if (requested_provider != "auto" &&
        capability.provider.id != requested_provider) {
      continue;
    }
    candidates.push_back(&capability);
  }
  if (!provider_registered) {
    return operation::Result<FormatProvider>::failure(
        {operation::ErrorCode::unsupported,
         "format provider '" + std::string{requested_provider} +
             "' is not registered",
         "inspect format list for available provider IDs"});
  }
  if (!format_registered) {
    return operation::Result<FormatProvider>::failure(
        {operation::ErrorCode::unsupported,
         "format '" + std::string{format_id} + "' is not registered",
         "select a format ID reported by format list"});
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto *left, const auto *right) {
                     const auto left_rank = provider_rank(left->provider);
                     const auto right_rank = provider_rank(right->provider);
                     return left_rank != right_rank
                                ? left_rank < right_rank
                                : left->provider.id < right->provider.id;
                   });
  const auto selected = std::find_if(
      candidates.begin(), candidates.end(), [direction](const auto *candidate) {
        return qualified(*candidate, direction);
      });
  if (selected == candidates.end()) {
    const auto *entry = candidates.empty()
                            ? nullptr
                            : direction_capability(*candidates.front(), direction);
    const auto reason = entry == nullptr
                            ? "direction capability is missing"
                            : !entry->available
                                  ? entry->unavailable_reason
                                  : "provider is not approved and available";
    return operation::Result<FormatProvider>::failure(
        {operation::ErrorCode::unsupported,
         "format '" + std::string{format_id} + "' is unavailable for " +
             std::string{to_string(direction)} + ": " + reason,
         "inspect format list for an available format-direction provider"});
  }
  return operation::Result<FormatProvider>::success((*selected)->provider);
}

operation::Result<FormatResolution>
resolve_format_extension(std::string_view extension, FormatDirection direction,
                         std::string_view requested_provider) {
  std::string normalized{extension};
  if (normalized.empty()) {
    return operation::Result<FormatResolution>::failure(
        {operation::ErrorCode::invalid_argument,
         "format extension is empty",
         "provide a file suffix such as .pdb or .xtc"});
  }
  if (normalized.front() != '.') normalized.insert(normalized.begin(), '.');
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (requested_provider.empty()) requested_provider = "auto";
  std::vector<const FormatCapability *> candidates;
  bool extension_registered = false;
  for (const auto &capability : format_capabilities()) {
    const auto matches = std::find(capability.extensions.begin(),
                                   capability.extensions.end(), normalized) !=
                         capability.extensions.end();
    if (!matches) continue;
    extension_registered = true;
    if (requested_provider != "auto" &&
        capability.provider.id != requested_provider) {
      continue;
    }
    if (qualified(capability, direction)) candidates.push_back(&capability);
  }
  if (candidates.empty()) {
    const auto subject = extension_registered ? "has no qualified provider for "
                                              : "is not registered for ";
    return operation::Result<FormatResolution>::failure(
        {operation::ErrorCode::unsupported,
         "extension '" + normalized + "' " + subject +
             std::string{to_string(direction)},
         "select an explicit format and provider reported by format list"});
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto *left, const auto *right) {
                     if (left->id != right->id) return left->id < right->id;
                     const auto left_rank = provider_rank(left->provider);
                     const auto right_rank = provider_rank(right->provider);
                     return left_rank != right_rank
                                ? left_rank < right_rank
                                : left->provider.id < right->provider.id;
                   });
  std::set<std::string, std::less<>> distinct_formats;
  for (const auto *candidate : candidates) {
    distinct_formats.insert(candidate->id);
  }
  if (distinct_formats.size() > 1U) {
    std::string formats;
    for (const auto &format : distinct_formats) {
      if (!formats.empty()) formats += ", ";
      formats += format;
    }
    return operation::Result<FormatResolution>::failure(
        {operation::ErrorCode::unsupported,
         "extension '" + normalized + "' is ambiguous across formats: " +
             formats,
         "set --file-format explicitly; provider selection never resolves a "
         "cross-format collision"});
  }
  const auto *selected = candidates.front();
  return operation::Result<FormatResolution>::success(
      {selected->id, normalized, direction, selected->provider});
}

} // namespace molshredder::io
