#ifndef REN_GLSL_INSTANCE_CULLING_H
#define REN_GLSL_INSTANCE_CULLING_H

#include "Indirect.h"
#include "Mesh.h"
#include "common.h"

GLSL_NAMESPACE_BEGIN

const uint INSTANCE_CULLING_THREADS = 128;

GLSL_BUFFER(4) BatchCommandOffsets { uint offset; };

GLSL_BUFFER(4) BatchCommandCounts { uint count; };

#define GLSL_INSTANCE_CULLING_CONSTANTS                                        \
  {                                                                            \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(CullMeshes) meshes;      \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(CullMeshInstances)       \
        mesh_instances;                                                        \
    GLSL_RESTRICT GLSL_READONLY GLSL_BUFFER_REFERENCE(BatchCommandOffsets)     \
        batch_command_offsets;                                                 \
    GLSL_RESTRICT GLSL_BUFFER_REFERENCE(BatchCommandCounts)                    \
        batch_command_counts;                                                  \
    GLSL_RESTRICT GLSL_WRITEONLY GLSL_BUFFER_REFERENCE(                        \
        DrawIndexedIndirectCommands) commands;                                 \
    uint num_mesh_instances;                                                   \
  }

GLSL_NAMESPACE_END

#endif // REN_GLSL_INSTANCE_CULLING_H
