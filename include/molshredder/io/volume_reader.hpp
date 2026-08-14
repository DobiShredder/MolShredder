#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/model/volume.hpp"
#include "molshredder/operation/common_types.hpp"
#include "molshredder/operation/result.hpp"

namespace molshredder::io {

enum class VolumeFormat { auto_detect, opendx, mrc };

struct VolumeData {
  std::string name;
  std::shared_ptr<const model::VolumeGrid> grid;
};

struct VolumeDocument {
  VolumeFormat format{VolumeFormat::auto_detect};
  std::vector<VolumeData> volumes;
};

struct VolumeReadOptions {
  VolumeFormat format{VolumeFormat::auto_detect};
  operation::LengthUnit coordinate_unit{operation::LengthUnit::angstrom};
  std::optional<std::string> name;
};

[[nodiscard]] operation::Result<VolumeDocument>
read_volume(std::string_view content, VolumeReadOptions options = {});

[[nodiscard]] operation::Result<VolumeDocument>
read_volume_file(const std::filesystem::path &path,
                 VolumeReadOptions options = {});

[[nodiscard]] std::string_view to_string(VolumeFormat format) noexcept;

} // namespace molshredder::io
