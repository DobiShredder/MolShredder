#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "molshredder/operation/result.hpp"

namespace molshredder::io {

inline constexpr unsigned int kFormatCapabilitySchemaVersion = 3U;

enum class FormatDirection { read, write };
enum class FormatProviderOrigin { native_builtin, dynamic_plugin, external_converter };
enum class FormatProviderTrust { trusted_builtin, trusted_configured, untrusted };
enum class FormatProviderLicenseStatus { approved, pending, rejected };

struct FormatProvider {
  std::string id;
  std::string version;
  FormatProviderOrigin origin{FormatProviderOrigin::native_builtin};
  FormatProviderTrust trust{FormatProviderTrust::trusted_builtin};
  FormatProviderLicenseStatus license_status{
      FormatProviderLicenseStatus::approved};
  std::string license_expression;
  bool available{true};
  std::string unavailable_reason;

  friend bool operator==(const FormatProvider &, const FormatProvider &) =
      default;
};

struct FormatDirectionCapability {
  FormatDirection direction{FormatDirection::read};
  bool available{};
  std::string unavailable_reason;
  std::vector<std::string> channels;
  std::vector<std::string> limitations;
  bool typed_loss_reporting{};

  friend bool operator==(const FormatDirectionCapability &,
                         const FormatDirectionCapability &) = default;
};

struct FormatCapabilityV2 {
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
  FormatProvider provider;
  std::vector<FormatDirectionCapability> directions;
  std::map<std::string, std::string, std::less<>> extension_fields;
};

struct FormatResolution {
  std::string format_id;
  std::string extension;
  FormatDirection direction{FormatDirection::read};
  FormatProvider provider;

  friend bool operator==(const FormatResolution &, const FormatResolution &) =
      default;
};

[[nodiscard]] const std::vector<FormatCapability>& format_capabilities();

[[nodiscard]] FormatCapability migrate_format_capability_v2(
    FormatCapabilityV2 capability,
    std::map<std::string, std::string, std::less<>> unknown_fields = {});

[[nodiscard]] const FormatDirectionCapability *direction_capability(
    const FormatCapability &capability, FormatDirection direction) noexcept;

[[nodiscard]] operation::Result<FormatProvider> resolve_format_provider(
    std::string_view format_id, FormatDirection direction,
    std::string_view requested_provider = "auto");

[[nodiscard]] operation::Result<FormatResolution> resolve_format_extension(
    std::string_view extension, FormatDirection direction,
    std::string_view requested_provider = "auto");

[[nodiscard]] std::string_view to_string(FormatDirection direction) noexcept;
[[nodiscard]] std::string_view to_string(FormatProviderOrigin origin) noexcept;
[[nodiscard]] std::string_view to_string(FormatProviderTrust trust) noexcept;
[[nodiscard]] std::string_view to_string(
    FormatProviderLicenseStatus status) noexcept;

}  // namespace molshredder::io
