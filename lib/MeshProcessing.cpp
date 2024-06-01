#include "MeshProcessing.hpp"
#include "MeshSimplification.hpp"

#include <meshoptimizer.h>
#include <mikktspace.h>

namespace ren {

auto mesh_process(const MeshProcessingOptions &opts) -> Mesh {
  Vector<glm::vec3> positions = opts.positions;
  Vector<glm::vec3> normals = opts.normals;
  Vector<glm::vec4> tangents = opts.tangents;
  Vector<glm::vec2> uvs = opts.uvs;
  Vector<glm::vec4> colors = opts.colors;

  ren_assert(positions.size() > 0);
  ren_assert(normals.size() == positions.size());
  if (not tangents.empty()) {
    ren_assert(tangents.size() == positions.size());
  }
  if (not uvs.empty()) {
    ren_assert(uvs.size() == positions.size());
  }
  if (not colors.empty()) {
    ren_assert(colors.size() == positions.size());
  }
  if (not opts.indices->empty()) {
    ren_assert(opts.indices->size() % 3 == 0);
  } else {
    ren_assert(positions.size() % 3 == 0);
  }

  Mesh mesh;

  // (Re)generate index buffer to remove duplicate vertices for LOD generation
  // to work correctly

  mesh_generate_indices({
      .positions = &positions,
      .normals = &normals,
      .tangents = tangents.size() ? &tangents : nullptr,
      .uvs = uvs.size() ? &uvs : nullptr,
      .colors = colors.size() ? &colors : nullptr,
      .indices = opts.indices,
  });

  // Generate tangents

  if (not uvs.empty() and tangents.empty()) {
    mesh_generate_tangents({
        .positions = &positions,
        .normals = &normals,
        .tangents = &tangents,
        .uvs = &uvs,
        .colors = colors.size() ? &colors : nullptr,
        .indices = opts.indices,
    });
  }

  // Generate LODs

  mesh_simplify({
      .positions = &positions,
      .normals = &normals,
      .tangents = tangents.size() ? &tangents : nullptr,
      .uvs = uvs.size() ? &uvs : nullptr,
      .colors = colors.size() ? &colors : nullptr,
      .indices = opts.indices,
      .lods = &mesh.lods,
  });

  u32 num_vertices = positions.size();
  mesh.num_indices = opts.indices->size();

  // Optimize each LOD separately

  for (const glsl::MeshLOD &lod : mesh.lods) {
    meshopt_optimizeVertexCache(&(*opts.indices)[lod.base_index],
                                &(*opts.indices)[lod.base_index],
                                lod.num_indices, num_vertices);
  }

  // Optimize all LODs together.

  {
    Vector<u32> remap(num_vertices);
    num_vertices = meshopt_optimizeVertexFetchRemap(
        remap.data(), opts.indices->data(), mesh.num_indices, num_vertices);
    meshopt_remapIndexBuffer(opts.indices->data(), opts.indices->data(),
                             mesh.num_indices, remap.data());
    mesh_remap_vertex_streams({
        .positions = &positions,
        .normals = &normals,
        .tangents = tangents.size() ? &tangents : nullptr,
        .uvs = uvs.size() ? &uvs : nullptr,
        .colors = colors.size() ? &colors : nullptr,
        .num_vertices = num_vertices,
        .remap = remap,
    });
  }

  // Encode vertex attributes

  *opts.enc_positions =
      mesh_encode_positions(positions, &mesh.bb, &mesh.pos_enc_bb);

  *opts.enc_normals = mesh_encode_normals(normals, mesh.pos_enc_bb);

  if (not tangents.empty()) {
    *opts.enc_tangents =
        mesh_encode_tangents(tangents, mesh.pos_enc_bb, *opts.enc_normals);
  }

  if (not uvs.empty()) {
    *opts.enc_uvs = mesh_encode_uvs(uvs, &mesh.uv_bs);
  }

  if (not colors.empty()) {
    *opts.enc_colors = mesh_encode_colors(colors);
  }

  return mesh;
}

void mesh_remap_vertex_streams(const MeshRemapVertexStreamsOptions &opts) {
  auto remap_stream = [&]<typename T>(Vector<T> *stream) {
    if (stream) {
      meshopt_remapVertexBuffer(stream->data(), stream->data(), stream->size(),
                                sizeof(T), opts.remap.data());
      stream->resize(opts.num_vertices);
    }
  };
  remap_stream(opts.positions.get());
  remap_stream(opts.normals.get());
  remap_stream(opts.tangents);
  remap_stream(opts.uvs);
  remap_stream(opts.colors);
}

void mesh_generate_indices(const MeshGenerateIndicesOptions &opts) {
  StaticVector<meshopt_Stream, 5> streams;
  auto add_stream = [&]<typename T>(Vector<T> *stream) {
    if (stream) {
      streams.push_back({
          .data = stream->data(),
          .size = sizeof(T),
          .stride = sizeof(T),
      });
    }
  };
  add_stream(opts.positions.get());
  add_stream(opts.normals.get());
  add_stream(opts.tangents);
  add_stream(opts.uvs);
  add_stream(opts.colors);
  const u32 *indices = nullptr;
  u32 num_vertices = opts.positions->size();
  u32 num_indices = num_vertices;
  if (not opts.indices->empty()) {
    indices = opts.indices->data();
    num_indices = opts.indices->size();
  };
  Vector<u32> remap(num_vertices);
  num_vertices = meshopt_generateVertexRemapMulti(
      remap.data(), indices, num_indices, num_vertices, streams.data(),
      streams.size());
  opts.indices->resize(num_indices);
  meshopt_remapIndexBuffer(opts.indices->data(), indices, num_indices,
                           remap.data());
  mesh_remap_vertex_streams({
      .positions = opts.positions,
      .normals = opts.normals,
      .tangents = opts.tangents,
      .uvs = opts.uvs,
      .colors = opts.colors,
      .num_vertices = num_vertices,
      .remap = remap,
  });
}

void mesh_generate_tangents(const MeshGenerateTangentsOptions &opts) {
  u32 num_vertices = opts.indices->size();

  auto unindex_stream = [&]<typename T>(Vector<T> &stream) {
    Vector<T> unindexed_stream(num_vertices);
    for (usize i = 0; i < num_vertices; ++i) {
      u32 index = (*opts.indices)[i];
      unindexed_stream[i] = stream[index];
    }
    std::swap(stream, unindexed_stream);
  };

  unindex_stream(*opts.positions);
  unindex_stream(*opts.normals);
  opts.tangents->resize(num_vertices);
  unindex_stream(*opts.uvs);
  if (opts.colors) {
    unindex_stream(*opts.colors);
  }
  opts.indices->clear();

  struct Context {
    size_t num_faces = 0;
    const glm::vec3 *positions = nullptr;
    const glm::vec3 *normals = nullptr;
    glm::vec4 *tangents = nullptr;
    const glm::vec2 *uvs = nullptr;
  };

  SMikkTSpaceInterface iface = {
      .m_getNumFaces = [](const SMikkTSpaceContext *pContext) -> int {
        return ((const Context *)(pContext->m_pUserData))->num_faces;
      },

      .m_getNumVerticesOfFace = [](const SMikkTSpaceContext *,
                                   const int) -> int { return 3; },

      .m_getPosition =
          [](const SMikkTSpaceContext *pContext, float fvPosOut[],
             const int iFace, const int iVert) {
            glm::vec3 position = ((const Context *)(pContext->m_pUserData))
                                     ->positions[iFace * 3 + iVert];
            fvPosOut[0] = position.x;
            fvPosOut[1] = position.y;
            fvPosOut[2] = position.z;
          },

      .m_getNormal =
          [](const SMikkTSpaceContext *pContext, float fvNormOut[],
             const int iFace, const int iVert) {
            glm::vec3 normal = ((const Context *)(pContext->m_pUserData))
                                   ->normals[iFace * 3 + iVert];
            fvNormOut[0] = normal.x;
            fvNormOut[1] = normal.y;
            fvNormOut[2] = normal.z;
          },

      .m_getTexCoord =
          [](const SMikkTSpaceContext *pContext, float fvTexcOut[],
             const int iFace, const int iVert) {
            glm::vec2 tex_coord = ((const Context *)(pContext->m_pUserData))
                                      ->uvs[iFace * 3 + iVert];
            fvTexcOut[0] = tex_coord.x;
            fvTexcOut[1] = tex_coord.y;
          },

      .m_setTSpaceBasic =
          [](const SMikkTSpaceContext *pContext, const float fvTangent[],
             const float fSign, const int iFace, const int iVert) {
            glm::vec4 &tangent = ((const Context *)(pContext->m_pUserData))
                                     ->tangents[iFace * 3 + iVert];
            tangent.x = fvTangent[0];
            tangent.y = fvTangent[1];
            tangent.z = fvTangent[2];
            tangent.w = -fSign;
          },
  };

  Context user_data = {
      .num_faces = num_vertices / 3,
      .positions = opts.positions->data(),
      .normals = opts.normals->data(),
      .tangents = opts.tangents->data(),
      .uvs = opts.uvs->data(),
  };

  SMikkTSpaceContext ctx = {
      .m_pInterface = &iface,
      .m_pUserData = &user_data,
  };

  genTangSpaceDefault(&ctx);

  mesh_generate_indices({
      .positions = opts.positions,
      .normals = opts.normals,
      .tangents = opts.tangents,
      .uvs = opts.uvs,
      .colors = opts.colors,
      .indices = opts.indices,
  });
}

auto mesh_encode_positions(Span<const glm::vec3> positions,
                           NotNull<glsl::PositionBoundingBox *> pbb,
                           NotNull<glm::vec3 *> enc_bb)
    -> Vector<glsl::Position> {
  glsl::BoundingBox bb = {
      .min = glm::vec3(std::numeric_limits<float>::infinity()),
      .max = -glm::vec3(std::numeric_limits<float>::infinity()),
  };

  // Select relatively big default bounding box size to avoid log2 NaN.
  *enc_bb = glm::vec3(1.0f);

  for (const glm::vec3 &position : positions) {
    *enc_bb = glm::max(*enc_bb, glm::abs(position));
    bb.min = glm::min(bb.min, position);
    bb.max = glm::max(bb.max, position);
  }

  *enc_bb = glm::exp2(glm::ceil(glm::log2(*enc_bb)));

  Vector<glsl::Position> enc_positions(positions.size());
  for (usize i = 0; i < positions.size(); ++i) {
    enc_positions[i] = glsl::encode_position(positions[i], *enc_bb);
  }

  *pbb = glsl::encode_bounding_box(bb, *enc_bb);

  return enc_positions;
}

auto mesh_encode_normals(Span<const glm::vec3> normals,
                         const glm::vec3 &pos_enc_bb) -> Vector<glsl::Normal> {
  glm::mat3 encode_transform_matrix =
      glsl::make_encode_position_matrix(pos_enc_bb);
  glm::mat3 encode_normal_matrix =
      glm::inverse(glm::transpose(encode_transform_matrix));

  Vector<glsl::Normal> enc_normals(normals.size());
  for (usize i = 0; i < normals.size(); ++i) {
    enc_normals[i] =
        glsl::encode_normal(glm::normalize(encode_normal_matrix * normals[i]));
  }

  return enc_normals;
}

auto mesh_encode_tangents(Span<const glm::vec4> tangents,
                          const glm::vec3 &pos_enc_bb,
                          Span<const glsl::Normal> enc_normals)
    -> Vector<glsl::Tangent> {
  glm::mat3 encode_transform_matrix =
      glsl::make_encode_position_matrix(pos_enc_bb);

  Vector<glsl::Tangent> enc_tangents(tangents.size());
  for (usize i = 0; i < tangents.size(); ++i) {
    // Encoding and then decoding the normal can change how the
    // tangent basis is selected due to rounding errors. Since
    // shaders use the decoded normal to decode the tangent, use
    // it for encoding as well.
    glm::vec3 normal = glsl::decode_normal(enc_normals[i]);

    // Orthonormalize tangent space.
    glm::vec4 tangent = tangents[i];
    glm::vec3 tangent3d(tangent);
    float sign = tangent.w;
    float proj = glm::dot(normal, tangent3d);
    tangent3d = tangent3d - proj * normal;

    tangent =
        glm::vec4(glm::normalize(encode_transform_matrix * tangent3d), sign);
    enc_tangents[i] = glsl::encode_tangent(tangent, normal);
  };

  return enc_tangents;
}

auto mesh_encode_uvs(Span<const glm::vec2> uvs,
                     NotNull<glsl::BoundingSquare *> uv_bs)
    -> Vector<glsl::UV> {
  for (glm::vec2 uv : uvs) {
    uv_bs->min = glm::min(uv_bs->min, uv);
    uv_bs->max = glm::max(uv_bs->max, uv);
  }

  // Round off the minimum and the maximum of the bounding square to the next
  // power of 2 if they are not equal to 0
  {
    // Select a relatively big default square size to avoid log2 NaN
    glm::vec2 p = glm::log2(glm::max(glm::max(-uv_bs->min, uv_bs->max), 1.0f));
    glm::vec2 bs = glm::exp2(glm::ceil(p));
    uv_bs->min = glm::mix(glm::vec2(0.0f), -bs,
                          glm::notEqual(uv_bs->min, glm::vec2(0.0f)));
    uv_bs->max = glm::mix(glm::vec2(0.0f), bs,
                          glm::notEqual(uv_bs->max, glm::vec2(0.0f)));
  }

  Vector<glsl::UV> enc_uvs(uvs.size());
  for (usize i = 0; i < uvs.size(); ++i) {
    enc_uvs[i] = glsl::encode_uv(uvs[i], *uv_bs);
  }

  return enc_uvs;
}

[[nodiscard]] auto mesh_encode_colors(Span<const glm::vec4> colors)
    -> Vector<glsl::Color> {
  Vector<glsl::Color> enc_colors(colors.size());
  for (usize i = 0; i < colors.size(); ++i) {
    enc_colors[i] = glsl::encode_color(colors[i]);
  }
  return enc_colors;
}

} // namespace ren
