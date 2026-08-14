#pragma once

#include <string>

#include "molshredder/application/dispatcher.hpp"

namespace molshredder::gui {

struct Action {
  std::string command_name;
  command::Arguments parameters;
};

class ActionAdapter {
 public:
  explicit ActionAdapter(const application::Dispatcher& dispatcher)
      : dispatcher_{dispatcher} {}

  [[nodiscard]] application::DispatchOutcome trigger(
      const Action& action, operation::TaskContext& context) const;

 private:
  const application::Dispatcher& dispatcher_;
};

}  // namespace molshredder::gui
