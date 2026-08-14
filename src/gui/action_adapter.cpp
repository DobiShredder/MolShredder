#include "molshredder/gui/action_adapter.hpp"

namespace molshredder::gui {

application::DispatchOutcome ActionAdapter::trigger(
    const Action& action, operation::TaskContext& context) const {
  return dispatcher_.dispatch(
      command::Invocation{action.command_name, action.parameters}, context);
}

}  // namespace molshredder::gui
