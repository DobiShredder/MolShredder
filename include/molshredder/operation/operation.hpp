#pragma once

#include <optional>
#include <string_view>

#include "molshredder/operation/error.hpp"
#include "molshredder/operation/result.hpp"
#include "molshredder/operation/task_context.hpp"

namespace molshredder::operation {

template <typename Request, typename Response>
class Operation {
 public:
  virtual ~Operation() = default;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  [[nodiscard]] virtual std::optional<Error> validate(
      const Request& request) const = 0;

  [[nodiscard]] virtual Result<Response> execute(
      const Request& request, TaskContext& context) const = 0;
};

}  // namespace molshredder::operation
