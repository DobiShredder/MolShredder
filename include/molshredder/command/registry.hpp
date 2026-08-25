#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "molshredder/command/result.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::command {

enum class ParameterType { text, integer, unsigned_integer, number, boolean };
enum class UndoPolicy { not_applicable, not_undoable, undoable };

struct ParameterSpec {
  ParameterSpec() = default;
  ParameterSpec(std::string parameter_name, ParameterType parameter_type,
                bool is_required,
                std::optional<std::string> parameter_default = std::nullopt,
                std::vector<std::string> choices = {})
      : name{std::move(parameter_name)},
        type{parameter_type},
        required{is_required},
        default_value{std::move(parameter_default)},
        allowed_values{std::move(choices)} {}

  std::string name;
  ParameterType type{ParameterType::text};
  bool required{false};
  std::optional<std::string> default_value;
  std::vector<std::string> allowed_values;
};

struct Descriptor {
  std::string canonical_name;
  std::string summary;
  std::vector<ParameterSpec> parameters;
  UndoPolicy undo_policy{UndoPolicy::not_applicable};
};

using Arguments = std::map<std::string, std::string, std::less<>>;

struct Invocation {
  std::string canonical_name;
  Arguments arguments;

  friend bool operator==(const Invocation&, const Invocation&) = default;
};

struct AliasSpec {
  std::string name;
  std::string canonical_name;
  Arguments defaults;

  friend bool operator==(const AliasSpec&, const AliasSpec&) = default;
};

enum class InvocationSource { cli, gui, python, session };

struct Provenance {
  unsigned int schema_version{1};
  InvocationSource source{InvocationSource::cli};
  unsigned long long sequence{};
  std::string canonical_command;

  friend bool operator==(const Provenance&, const Provenance&) = default;
};

struct InvocationRecord {
  Invocation invocation;
  Provenance provenance;
};

using Handler = std::function<operation::Result<Response>(
    const Arguments&, operation::TaskContext&)>;

class Registry {
 public:
  [[nodiscard]] std::optional<operation::Error> add(
      Descriptor descriptor, Handler handler);

  [[nodiscard]] std::optional<operation::Error> add_alias(AliasSpec alias);

  [[nodiscard]] operation::Result<Invocation> expand(
      const Invocation& invocation) const;

  [[nodiscard]] operation::Result<Invocation> normalize(
      const Invocation& invocation) const;

  [[nodiscard]] operation::Result<Response> invoke(
      std::string_view canonical_name, const Arguments& arguments,
      operation::TaskContext& context) const;

  [[nodiscard]] operation::Result<Response> invoke(
      const Invocation& invocation, operation::TaskContext& context) const;

  [[nodiscard]] std::vector<Descriptor> descriptors() const;

  [[nodiscard]] std::vector<AliasSpec> aliases() const;

 private:
  struct Entry {
    Descriptor descriptor;
    Handler handler;
  };

  std::map<std::string, Entry, std::less<>> entries_;
  std::map<std::string, AliasSpec, std::less<>> aliases_;
};

}  // namespace molshredder::command
