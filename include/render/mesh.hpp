#ifndef MESH_H
#define MESH_H

#include <render/buffer.hpp>
#include <render/render_common.hpp>
#include <concepts>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <vector>
#include <ozz/base/maths/simd_math.h>

namespace vke_render
{
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;

        Vertex() = default;
        Vertex(glm::vec3 pos, glm::vec3 normal, glm::vec4 tangent, glm::vec2 texCoord)
            : pos(pos), normal(normal), tangent(tangent), texCoord(texCoord) {}
    };

    struct SkinVertex
    {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
        glm::vec4 weights;
        glm::uvec4 jointIDs;
    };

    struct MeshInfo
    {
        uint64_t vertexOffset;
        uint64_t vertexSize;
        uint64_t vertexCnt;
        uint64_t indexOffset;
        uint64_t indexSize;
        uint64_t indexCnt;

        MeshInfo() = default;
        MeshInfo(uint64_t voffset, uint64_t vsize, uint64_t vcnt,
                 uint64_t ioffset, uint64_t isize, uint64_t icnt)
            : vertexOffset(voffset), vertexSize(vsize), vertexCnt(vcnt), indexOffset(ioffset), indexSize(isize), indexCnt(icnt) {}

        uint64_t getVertexUnitSize() const { return vertexSize / vertexCnt; }
        uint64_t getIndexUnitSize() const { return indexSize / indexCnt; }
    };

    template <typename T>
    concept AllowedIndexType = std::same_as<T, uint16_t> || std::same_as<T, uint32_t>;

    class Mesh
    {
    public:
        vke_common::AssetHandle handle;
        std::unique_ptr<DeviceBuffer> vertexBuffer;
        std::unique_ptr<DeviceBuffer> indexBuffer;
        std::vector<ozz::math::Float4x4> invBindMatrices;
        std::vector<int> joints;
        std::vector<MeshInfo> infos;
        AABB bounds;

        Mesh() : handle(0), vertexBuffer(nullptr), indexBuffer(nullptr) {}

        Mesh(const vke_common::AssetHandle hdl) : handle(hdl), vertexBuffer(nullptr), indexBuffer(nullptr) {}

        Mesh(const vke_common::AssetHandle hdl, const CPUBuffer<> &vbuffer, const CPUBuffer<> &ibuffer, std::vector<MeshInfo> &&infos)
            : handle(hdl),
              vertexBuffer(std::make_unique<DeviceBuffer>(vbuffer.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
              indexBuffer(std::make_unique<DeviceBuffer>(ibuffer.size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
              infos(std::move(infos))
        {
            bounds = calculateBounds(this->infos, vbuffer.data, ibuffer.data);
            vertexBuffer->ToBuffer(0, vbuffer.data, vbuffer.size);
            indexBuffer->ToBuffer(0, ibuffer.data, ibuffer.size);
        }

        template <typename VT, AllowedIndexType IT>
        Mesh(const vke_common::AssetHandle hdl, const std::span<const VT> vertices, const std::span<const IT> indices, std::vector<MeshInfo> &&infos)
            : handle(hdl),
              vertexBuffer(std::make_unique<DeviceBuffer>(vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
              indexBuffer(std::make_unique<DeviceBuffer>(indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT)),
              infos(std::move(infos))
        {
            bounds = calculateBounds(this->infos, vertices.data(), indices.data());
            vertexBuffer->ToBuffer(0, vertices.data(), vertices.size_bytes());
            indexBuffer->ToBuffer(0, indices.data(), indices.size_bytes());
        }

        template <typename VT, AllowedIndexType IT>
        Mesh(const vke_common::AssetHandle hdl, const std::span<const VT> vertices, const std::span<const IT> indices)
            : handle(hdl),
              vertexBuffer(std::make_unique<DeviceBuffer>(vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)),
              indexBuffer(std::make_unique<DeviceBuffer>(indices.size_bytes(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
        {
            vertexBuffer->ToBuffer(0, vertices.data(), vertices.size_bytes());
            indexBuffer->ToBuffer(0, indices.data(), indices.size_bytes());
            infos.emplace_back(0, vertexBuffer->bufferSize, vertices.size(), 0, indexBuffer->bufferSize, indices.size());
            bounds = calculateBounds(infos, vertices.data(), indices.data());
        }

        static vke_common::Result<std::unique_ptr<Mesh>> Create(vke_common::AssetHandle hdl, std::span<const char> data)
        {
            auto read = [&data](void *destination, size_t size)
            {
                if (size > data.size()) return false;
                if (size != 0) std::memcpy(destination, data.data(), size);
                data = data.subspan(size);
                return true;
            };
            auto mesh = std::make_unique<Mesh>(hdl);
            uint32_t infoCount;
            if (!read(&mesh->bounds.min, 3 * sizeof(float)) ||
                !read(&mesh->bounds.max, 3 * sizeof(float)) || !read(&infoCount, sizeof(infoCount)))
                return std::unexpected("truncated mesh header");
            if (infoCount == 0 || infoCount > data.size() / sizeof(MeshInfo))
                return std::unexpected("invalid mesh info count");
            mesh->infos.resize(infoCount);
            const size_t infoSize = infoCount * sizeof(MeshInfo);
            std::memcpy(mesh->infos.data(), data.data(), infoSize);
            data = data.subspan(infoSize);

            uint64_t vertexSize, indexSize;
            if (!read(&vertexSize, sizeof(vertexSize)) || vertexSize == 0 || vertexSize > data.size())
                return std::unexpected("invalid or truncated vertex buffer");
            const auto vertices = data.first(static_cast<size_t>(vertexSize));
            data = data.subspan(vertices.size());
            if (!read(&indexSize, sizeof(indexSize)) || indexSize == 0 || indexSize > data.size())
                return std::unexpected("invalid or truncated index buffer");
            const auto indices = data.first(static_cast<size_t>(indexSize));
            data = data.subspan(indices.size());

            for (size_t i = 0; i < mesh->infos.size(); ++i)
            {
                const auto &info = mesh->infos[i];
                auto primitiveError = [i](const char *message)
                { return std::unexpected("primitive " + std::to_string(i) + ": " + message); };
                if (info.vertexCnt == 0 || info.indexCnt == 0 ||
                    info.vertexOffset > vertexSize || info.vertexSize > vertexSize - info.vertexOffset ||
                    info.indexOffset > indexSize || info.indexSize > indexSize - info.indexOffset ||
                    info.vertexSize % info.vertexCnt != 0 || info.indexSize % info.indexCnt != 0)
                    return primitiveError("invalid buffer range or element count");
                const auto vertexStride = info.getVertexUnitSize();
                const auto indexStride = info.getIndexUnitSize();
                if (vertexStride < 3 * sizeof(float) || (indexStride != 2 && indexStride != 4) ||
                    info.vertexOffset % vertexStride != 0 || info.indexOffset % indexStride != 0)
                    return primitiveError("invalid vertex or index layout");
                if (info.indexCnt > std::numeric_limits<uint32_t>::max() ||
                    info.indexOffset / indexStride > std::numeric_limits<uint32_t>::max() ||
                    info.vertexOffset / vertexStride > std::numeric_limits<int32_t>::max())
                    return primitiveError("draw range exceeds Vulkan limits");
                for (uint64_t j = 0; j < info.indexCnt; ++j)
                {
                    uint32_t index = 0;
                    std::memcpy(&index, indices.data() + info.indexOffset + j * indexStride, indexStride);
                    if (index >= info.vertexCnt)
                        return primitiveError("index references a vertex outside the primitive");
                }
            }

            if (!data.empty())
            {
                uint64_t matrixSize;
                constexpr size_t matrixStride = 16 * sizeof(float);
                if (!read(&matrixSize, sizeof(matrixSize)) || matrixSize % matrixStride != 0 || matrixSize > data.size())
                    return std::unexpected("invalid or truncated inverse bind matrices");
                const char *matrixData = data.data();
                data = data.subspan(static_cast<size_t>(matrixSize));
                mesh->invBindMatrices.resize(static_cast<size_t>(matrixSize / matrixStride));
                for (auto &matrix : mesh->invBindMatrices)
                    for (auto &column : matrix.cols)
                    {
                        float values[4];
                        std::memcpy(values, matrixData, sizeof(values));
                        matrixData += sizeof(values);
                        column = ozz::math::simd_float4::LoadPtrU(values);
                    }
                uint32_t jointCount;
                if (!read(&jointCount, sizeof(jointCount)) || jointCount > data.size() / sizeof(int))
                    return std::unexpected("invalid or truncated joint list");
                mesh->joints.resize(jointCount);
                const size_t jointSize = jointCount * sizeof(int);
                if (jointSize != 0) std::memcpy(mesh->joints.data(), data.data(), jointSize);
                data = data.subspan(jointSize);
                if (!mesh->invBindMatrices.empty() && mesh->joints.size() != mesh->invBindMatrices.size())
                    return std::unexpected("joint and inverse bind matrix counts differ");
                if (!data.empty()) return std::unexpected("unexpected trailing mesh data");
            }

            mesh->vertexBuffer = std::make_unique<DeviceBuffer>(vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            mesh->vertexBuffer->ToBuffer(0, vertices.data(), vertices.size());
            mesh->indexBuffer = std::make_unique<DeviceBuffer>(indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            mesh->indexBuffer->ToBuffer(0, indices.data(), indices.size());
            return mesh;
        }

        ~Mesh() {}

        void Render(VkCommandBuffer &commandBuffer) const
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vertexBuffer->buffer), &offset);

            VkIndexType prevIndexType = VK_INDEX_TYPE_MAX_ENUM;
            for (auto &info : infos)
            {
                VkIndexType indexType = getIndexType(info);
                if (indexType != prevIndexType)
                {
                    vkCmdBindIndexBuffer(commandBuffer, indexBuffer->buffer, 0, indexType);
                    prevIndexType = indexType;
                }

                vkCmdDrawIndexed(commandBuffer, info.indexCnt, 1,
                                 info.indexOffset / info.getIndexUnitSize(),
                                 info.vertexOffset / info.getVertexUnitSize(), 0);
            }
        }

        void RenderPrimitive(VkCommandBuffer &commandBuffer, uint32_t idx, VkIndexType &prevIndexType) const
        {
            if (idx == 0)
            {
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, &(vertexBuffer->buffer), &offset);
            }
            auto &info = infos[idx];
            VkIndexType indexType = getIndexType(info);
            if (indexType != prevIndexType)
            {
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer->buffer, 0, indexType);
                prevIndexType = indexType;
            }

            vkCmdDrawIndexed(commandBuffer, info.indexCnt, 1,
                             info.indexOffset / info.getIndexUnitSize(),
                             info.vertexOffset / info.getVertexUnitSize(), 0);
        }

        template <typename VT, AllowedIndexType IT>
        static void MeshDataToBinary(std::ostream &binary, const std::vector<MeshInfo> &infos,
                                     const std::span<const VT> &vertices, const std::span<const IT> &indices)
        {
            uint64_t vsize = vertices.size_bytes();
            uint64_t isize = indices.size_bytes();
            writeHeader(binary, infos, vertices.data(), indices.data());
            binary.write((const char *)&vsize, sizeof(uint64_t));
            binary.write((const char *)(vertices.data()), vsize);
            binary.write((const char *)&isize, sizeof(uint64_t));
            binary.write((const char *)(indices.data()), isize);
        }

        static void MeshDataToBinary(std::ostream &binary, const std::vector<MeshInfo> &infos,
                                     const CPUBuffer<> &vertices, const CPUBuffer<> &indices)
        {
            writeHeader(binary, infos, vertices.data, indices.data);
            vertices.WriteToBinaryStream(binary);
            indices.WriteToBinaryStream(binary);
        }

        static void MeshDataToBinary(std::ostream &binary, const std::vector<MeshInfo> &infos,
                                     const CPUBuffer<> &vertices, const CPUBuffer<> &indices,
                                     const CPUBuffer<> &invBindMatrices, const std::vector<int> &joints)
        {
            MeshDataToBinary(binary, infos, vertices, indices);
            invBindMatrices.WriteToBinaryStream(binary);
            uint32_t jointCnt = joints.size();
            binary.write((const char *)&jointCnt, sizeof(uint32_t));
            binary.write((const char *)(joints.data()), jointCnt * sizeof(int));
        }

    private:
        static void writeHeader(std::ostream &binary, const std::vector<MeshInfo> &infos,
                                const void *vertices, const void *indices)
        {
            const AABB bounds = calculateBounds(infos, vertices, indices);
            const uint32_t infoCnt = infos.size();
            binary.write((const char *)&bounds.min, 3 * sizeof(float));
            binary.write((const char *)&bounds.max, 3 * sizeof(float));
            binary.write((const char *)&infoCnt, sizeof(infoCnt));
            binary.write((const char *)infos.data(), infoCnt * sizeof(MeshInfo));
        }

        static AABB calculateBounds(const std::vector<MeshInfo> &infos, const void *vertices, const void *indices)
        {
            AABB bounds{glm::vec3(std::numeric_limits<float>::max()),
                        glm::vec3(std::numeric_limits<float>::lowest())};
            bool hasIndices = false;
            const char *vertexData = static_cast<const char *>(vertices);
            const char *indexData = static_cast<const char *>(indices);
            // Every mesh vertex layout starts with its local-space position.
            for (const MeshInfo &info : infos)
            {
                if (info.indexCnt == 0)
                    continue;
                hasIndices = true;
                const uint64_t stride = info.getVertexUnitSize();
                const uint64_t indexStride = info.getIndexUnitSize();
                for (uint64_t i = 0; i < info.indexCnt; ++i)
                {
                    uint32_t index;
                    const char *source = indexData + info.indexOffset + i * indexStride;
                    if (indexStride == sizeof(uint16_t))
                    {
                        uint16_t shortIndex;
                        std::memcpy(&shortIndex, source, sizeof(shortIndex));
                        index = shortIndex;
                    }
                    else
                        std::memcpy(&index, source, sizeof(index));
                    glm::vec3 position;
                    std::memcpy(&position, vertexData + info.vertexOffset + index * stride, 3 * sizeof(float));
                    bounds.min = glm::min(bounds.min, position);
                    bounds.max = glm::max(bounds.max, position);
                }
            }
            return hasIndices ? bounds : AABB{};
        }

        VkIndexType getIndexType(const MeshInfo &info) const
        {
            if (info.getIndexUnitSize() == 2)
                return VK_INDEX_TYPE_UINT16;
            return VK_INDEX_TYPE_UINT32;
        }
    };
}

#endif
