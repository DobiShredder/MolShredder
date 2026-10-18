#pragma once

#include <ostream>

#include "molshredder/io/structure_writer.hpp"

namespace molshredder::io::detail {

[[nodiscard]] operation::Result<StructureWriteReport>
write_mmcif(std::ostream &output, const model::Topology &topology,
            const model::CoordinateSource &coordinates,
            const StructureWriteOptions &options,
            operation::TaskContext &context);

[[nodiscard]] operation::Result<StructureWriteReport>
write_mmtf(std::ostream &output, const model::Topology &topology,
           const model::CoordinateSource &coordinates,
           const StructureWriteOptions &options,
           operation::TaskContext &context);

[[nodiscard]] operation::Result<StructureWriteReport>
write_mmod(std::ostream &output, const model::Topology &topology,
           const model::CoordinateSource &coordinates,
           const StructureWriteOptions &options,
           operation::TaskContext &context);

[[nodiscard]] operation::Result<StructureWriteReport>
write_mae(std::ostream &output, const model::Topology &topology,
          const model::CoordinateSource &coordinates,
          const StructureWriteOptions &options,
          operation::TaskContext &context);

} // namespace molshredder::io::detail
