#pragma once

#include <ostream>

#include "molshredder/io/structure_writer.hpp"

namespace molshredder::io::detail {

[[nodiscard]] operation::Result<StructureWriteReport>
write_mmcif(std::ostream &output, const model::Topology &topology,
            const model::CoordinateSource &coordinates,
            const StructureWriteOptions &options,
            operation::TaskContext &context);

} // namespace molshredder::io::detail
