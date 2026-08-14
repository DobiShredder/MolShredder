#pragma once

#include <string>
#include <vector>

namespace molshredder::io {

inline constexpr unsigned int kFormatCapabilitySchemaVersion = 2U;

struct FormatCapability {
  std::string id;
  std::string family;
  std::vector<std::string> extensions;
  bool readable{};
  bool writable{};
  bool multi_frame{};
  bool multi_structure{};
  bool random_access{};
  bool streaming{};
  std::vector<std::string> channels;
  std::vector<std::string> limitations;
  std::string implementation;
};

[[nodiscard]] const std::vector<FormatCapability>& format_capabilities();

}  // namespace molshredder::io
