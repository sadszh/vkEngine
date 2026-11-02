#define GLM_ENABLE_EXPERIMENTAL

#include <common.hpp>
#include <render/mesh.hpp>
#include <tinygltf/tiny_gltf.h>
#include <nlohmann/json.hpp>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr uint64_t FIRST_ASSET_ID = 1024;
    constexpr int ASSET_TEXTURE = 0;
    constexpr int ASSET_MESH = 1;
    constexpr int ASSET_VF_SHADER = 2;
    constexpr int ASSET_MATERIAL = 4;
    constexpr int ASSET_SCENE = 7;

    constexpr uint32_t SHADER_BASE_COLOR_TEXTURE = 1u << 0;
    constexpr uint32_t SHADER_METALLIC_ROUGHNESS_TEXTURE = 1u << 1;
    constexpr uint32_t SHADER_NORMAL_TEXTURE = 1u << 2;
    constexpr uint32_t SHADER_OCCLUSION_TEXTURE = 1u << 3;
    constexpr uint32_t SHADER_TRANSPARENT = 1u << 4;

    struct AssetIds
    {
        uint64_t texture = FIRST_ASSET_ID;
        uint64_t mesh = FIRST_ASSET_ID;
        uint64_t shader = FIRST_ASSET_ID;
        uint64_t material = FIRST_ASSET_ID;
        uint64_t scene = 1;
    };

    struct PrimitiveAsset
    {
        uint64_t mesh = 0;
        uint64_t material = 0;
    };

    struct TransformData
    {
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct Converter
    {
        tinygltf::Model model;
        fs::path inputPath;
        fs::path resourceDir;
        fs::path outputDir;
        std::string baseName;
        bool meshOnly = false;
        json assets = json::array();
        AssetIds ids;
        std::map<std::pair<int, bool>, uint64_t> textureAssets;
        std::map<int, uint64_t> materialAssets;
        std::map<uint32_t, uint64_t> shaderAssets;
        std::map<std::pair<int, int>, PrimitiveAsset> primitiveAssets;
        fs::path commonVertexSourcePath;
        fs::path commonVertexBinaryPath;
        bool commonVertexShaderGenerated = false;

        bool Run();
        bool ExportPrimitive(int meshIndex, int primitiveIndex, PrimitiveAsset &out);
        bool ExportMaterial(int materialIndex, uint64_t &out);
        bool ExportTexture(int textureIndex, bool srgb, uint64_t &out);
        bool ExportShader(int materialIndex, uint64_t &out);
        bool ExportScenes();
        bool WriteScene(size_t sceneIndex);
        json MakeObject(uint32_t id, const std::string &name, uint32_t parent,
                        const TransformData &transform, json components, json children) const;
    };

    void LogError(const std::string &message)
    {
        std::cerr << "gltf_scene_conv: " << message << '\n';
    }

    std::string SafeName(std::string value)
    {
        if (value.empty())
            return "unnamed";
        for (char &c : value)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
                c = '_';
        return value;
    }

    bool WriteJSON(const fs::path &path, const json &value)
    {
        std::ofstream output(path);
        if (!output)
        {
            LogError("cannot write " + path.string());
            return false;
        }
        output << value.dump(2) << '\n';
        return output.good();
    }

    bool WriteText(const fs::path &path, const std::string &value)
    {
        std::ofstream output(path);
        if (!output)
        {
            LogError("cannot write " + path.string());
            return false;
        }
        output << value;
        return output.good();
    }

    std::string PathString(const fs::path &path)
    {
        return path.lexically_normal().generic_string();
    }

    double ReadComponent(const unsigned char *data, int componentType, bool normalized)
    {
        switch (componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        {
            const auto value = *reinterpret_cast<const int8_t *>(data);
            return normalized ? std::max(-1.0, value / 127.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            const auto value = *reinterpret_cast<const uint8_t *>(data);
            return normalized ? value / 255.0 : value;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        {
            int16_t value;
            std::memcpy(&value, data, sizeof(value));
            return normalized ? std::max(-1.0, value / 32767.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            uint16_t value;
            std::memcpy(&value, data, sizeof(value));
            return normalized ? value / 65535.0 : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            uint32_t value;
            std::memcpy(&value, data, sizeof(value));
            return normalized ? value / 4294967295.0 : value;
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
        {
            float value;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        {
            double value;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        default:
            return 0.0;
        }
    }

    bool AccessorData(const tinygltf::Model &model, int accessorIndex,
                      std::vector<double> &values, int &componentCount)
    {
        if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            return false;
        const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
        componentCount = tinygltf::GetNumComponentsInType(accessor.type);
        const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
        if (componentCount <= 0 || componentSize <= 0)
            return false;

        values.assign(accessor.count * componentCount, 0.0);
        if (accessor.bufferView >= 0)
        {
            if (accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
                return false;
            const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
            if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size()))
                return false;
            const tinygltf::Buffer &buffer = model.buffers[view.buffer];
            const int byteStride = accessor.ByteStride(view);
            if (byteStride <= 0)
                return false;
            const size_t start = view.byteOffset + accessor.byteOffset;
            const size_t required = accessor.count == 0
                                        ? start
                                        : start + (accessor.count - 1) * byteStride +
                                              componentCount * componentSize;
            if (required > buffer.data.size())
                return false;
            for (size_t i = 0; i < accessor.count; ++i)
            {
                const unsigned char *element = buffer.data.data() + start + i * byteStride;
                for (int c = 0; c < componentCount; ++c)
                    values[i * componentCount + c] =
                        ReadComponent(element + c * componentSize, accessor.componentType,
                                      accessor.normalized);
            }
        }

        if (!accessor.sparse.isSparse || accessor.sparse.count <= 0)
            return true;
        const auto &sparse = accessor.sparse;
        if (sparse.indices.bufferView < 0 || sparse.values.bufferView < 0)
            return false;
        const tinygltf::BufferView &indexView = model.bufferViews[sparse.indices.bufferView];
        const tinygltf::BufferView &valueView = model.bufferViews[sparse.values.bufferView];
        const tinygltf::Buffer &indexBuffer = model.buffers[indexView.buffer];
        const tinygltf::Buffer &valueBuffer = model.buffers[valueView.buffer];
        const int indexSize = tinygltf::GetComponentSizeInBytes(sparse.indices.componentType);
        const size_t indexStart = indexView.byteOffset + sparse.indices.byteOffset;
        const size_t valueStart = valueView.byteOffset + sparse.values.byteOffset;
        if (indexSize <= 0 ||
            indexStart + static_cast<size_t>(sparse.count) * indexSize > indexBuffer.data.size() ||
            valueStart + static_cast<size_t>(sparse.count) * componentCount * componentSize >
                valueBuffer.data.size())
            return false;

        for (int i = 0; i < sparse.count; ++i)
        {
            const size_t target = static_cast<size_t>(ReadComponent(
                indexBuffer.data.data() + indexStart + i * indexSize,
                sparse.indices.componentType, false));
            if (target >= accessor.count)
                return false;
            const unsigned char *element =
                valueBuffer.data.data() + valueStart + i * componentCount * componentSize;
            for (int c = 0; c < componentCount; ++c)
                values[target * componentCount + c] =
                    ReadComponent(element + c * componentSize, accessor.componentType,
                                  accessor.normalized);
        }
        return true;
    }

    template <typename T>
    bool ReadAttribute(const tinygltf::Model &model, const tinygltf::Primitive &primitive,
                       const char *name, int expectedComponents, std::vector<T> &output)
    {
        auto it = primitive.attributes.find(name);
        if (it == primitive.attributes.end())
            return false;
        std::vector<double> values;
        int components = 0;
        if (!AccessorData(model, it->second, values, components) || components != expectedComponents)
            return false;
        output.resize(values.size() / components);
        for (size_t i = 0; i < output.size(); ++i)
            for (int c = 0; c < components; ++c)
                output[i][c] = static_cast<float>(values[i * components + c]);
        return true;
    }

    bool ReadIndices(const tinygltf::Model &model, const tinygltf::Primitive &primitive,
                     size_t vertexCount, std::vector<uint32_t> &indices)
    {
        if (primitive.indices < 0)
        {
            indices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i)
                indices[i] = static_cast<uint32_t>(i);
            return true;
        }
        std::vector<double> values;
        int components = 0;
        if (!AccessorData(model, primitive.indices, values, components) || components != 1)
            return false;
        indices.resize(values.size());
        for (size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] < 0.0 || values[i] >= static_cast<double>(vertexCount))
                return false;
            indices[i] = static_cast<uint32_t>(values[i]);
        }
        return true;
    }

    bool Triangulate(int mode, const std::vector<uint32_t> &source,
                     std::vector<uint32_t> &triangles)
    {
        if (mode == -1 || mode == TINYGLTF_MODE_TRIANGLES)
        {
            if (source.size() % 3 != 0)
                return false;
            triangles = source;
            return true;
        }
        if (mode == TINYGLTF_MODE_TRIANGLE_STRIP)
        {
            for (size_t i = 2; i < source.size(); ++i)
            {
                const uint32_t a = source[i - 2];
                const uint32_t b = source[i - 1];
                const uint32_t c = source[i];
                if (a == b || b == c || a == c)
                    continue;
                if ((i & 1) == 0)
                    triangles.insert(triangles.end(), {a, b, c});
                else
                    triangles.insert(triangles.end(), {b, a, c});
            }
            return true;
        }
        if (mode == TINYGLTF_MODE_TRIANGLE_FAN)
        {
            for (size_t i = 2; i < source.size(); ++i)
                if (source[0] != source[i - 1] && source[i - 1] != source[i] &&
                    source[0] != source[i])
                    triangles.insert(triangles.end(), {source[0], source[i - 1], source[i]});
            return true;
        }
        return false;
    }

    void GenerateNormals(std::vector<vke_render::Vertex> &vertices,
                         const std::vector<uint32_t> &indices)
    {
        for (vke_render::Vertex &vertex : vertices)
            vertex.normal = glm::vec3(0.0f);
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            const glm::vec3 normal = glm::cross(vertices[ib].pos - vertices[ia].pos,
                                                vertices[ic].pos - vertices[ia].pos);
            vertices[ia].normal += normal;
            vertices[ib].normal += normal;
            vertices[ic].normal += normal;
        }
        for (vke_render::Vertex &vertex : vertices)
            vertex.normal = glm::length2(vertex.normal) > 0.0f
                                ? glm::normalize(vertex.normal)
                                : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    void GenerateTangents(std::vector<vke_render::Vertex> &vertices,
                          const std::vector<uint32_t> &indices)
    {
        std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            const glm::vec3 e1 = vertices[ib].pos - vertices[ia].pos;
            const glm::vec3 e2 = vertices[ic].pos - vertices[ia].pos;
            const glm::vec2 d1 = vertices[ib].texCoord - vertices[ia].texCoord;
            const glm::vec2 d2 = vertices[ic].texCoord - vertices[ia].texCoord;
            const float determinant = d1.x * d2.y - d1.y * d2.x;
            if (std::abs(determinant) < 1e-8f)
                continue;
            const float r = 1.0f / determinant;
            const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * r;
            const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * r;
            for (uint32_t index : {ia, ib, ic})
            {
                tangents[index] += tangent;
                bitangents[index] += bitangent;
            }
        }
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const glm::vec3 n = vertices[i].normal;
            glm::vec3 t = tangents[i] - n * glm::dot(n, tangents[i]);
            if (glm::length2(t) < 1e-8f)
            {
                const glm::vec3 axis = std::abs(n.z) < 0.999f
                                           ? glm::vec3(0.0f, 0.0f, 1.0f)
                                           : glm::vec3(0.0f, 1.0f, 0.0f);
                t = glm::normalize(glm::cross(axis, n));
            }
            else
                t = glm::normalize(t);
            const float handedness = glm::dot(glm::cross(n, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
            vertices[i].tangent = glm::vec4(t, handedness);
        }
    }

    TransformData NodeTransform(const tinygltf::Node &node)
    {
        TransformData result;
        if (node.matrix.size() == 16)
        {
            glm::mat4 matrix(1.0f);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    matrix[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(matrix, result.scale, result.rotation, result.position, skew, perspective);
            result.rotation = glm::normalize(result.rotation);
            return result;
        }
        if (node.translation.size() == 3)
            result.position = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
        if (node.scale.size() == 3)
            result.scale = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
        if (node.rotation.size() == 4)
            result.rotation = glm::normalize(glm::quat(
                static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
        return result;
    }

    json TransformJSON(const TransformData &transform)
    {
        return {
            {"pos", {transform.position.x, transform.position.y, transform.position.z}},
            {"scl", {transform.scale.x, transform.scale.y, transform.scale.z}},
            {"rot", {transform.rotation.x, transform.rotation.y, transform.rotation.z,
                     transform.rotation.w}}};
    }

    std::string ImageExtension(const tinygltf::Image &image)
    {
        if (image.mimeType == "image/jpeg")
            return ".jpg";
        if (image.mimeType == "image/webp")
            return ".webp";
        if (image.mimeType == "image/bmp")
            return ".bmp";
        if (image.mimeType == "image/gif")
            return ".gif";
        return ".png";
    }

    VkFilter TextureFilter(int filter)
    {
        return filter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
                       filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                       filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR
                   ? VK_FILTER_NEAREST
                   : VK_FILTER_LINEAR;
    }

    VkSamplerAddressMode TextureAddressMode(int wrap)
    {
        if (wrap == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE)
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (wrap == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT)
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    std::string VertexShaderSource()
    {
        return R"(#version 450
layout(set = 0, binding = 0) uniform CameraBlockObject {
    float nearPlane; float farPlane; float fov; float aspect;
    mat4 view; mat4 projection; mat4 invView; mat4 invProjection; vec4 viewPos;
} CameraInfo;
layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) vec4 materialFactors;
    layout(offset = 96) vec4 emissiveAlpha;
    layout(offset = 112) uvec4 forwardInfo;
} constants;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inTexCoord;
layout(location = 0) out vec3 vViewPos;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out mat3 vTBN;
void main() {
    vec4 viewPos = CameraInfo.view * constants.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(CameraInfo.view * constants.model)));
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    T = normalize(T - N * dot(N, T));
    vec3 B = normalize(cross(N, T) * inTangent.w);
    vViewPos = viewPos.xyz;
    vTexCoord = inTexCoord;
    vTBN = mat3(T, B, N);
    gl_Position = CameraInfo.projection * viewPos;
}
)";
    }

    uint32_t ShaderFeatureMask(const tinygltf::Material &material)
    {
        uint32_t mask = 0;
        if (material.pbrMetallicRoughness.baseColorTexture.index >= 0)
            mask |= SHADER_BASE_COLOR_TEXTURE;
        if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
            mask |= SHADER_METALLIC_ROUGHNESS_TEXTURE;
        if (material.normalTexture.index >= 0)
            mask |= SHADER_NORMAL_TEXTURE;
        if (material.occlusionTexture.index >= 0)
            mask |= SHADER_OCCLUSION_TEXTURE;
        return mask;
    }

    std::string ShaderFeatureName(uint32_t mask)
    {
        std::string name = "pbr";
        name += (mask & SHADER_BASE_COLOR_TEXTURE) != 0 ? "_base_tex" : "_base_const";
        name += (mask & SHADER_METALLIC_ROUGHNESS_TEXTURE) != 0 ? "_mr_tex" : "_mr_const";
        name += (mask & SHADER_NORMAL_TEXTURE) != 0 ? "_normal_tex" : "_normal_vertex";
        name += (mask & SHADER_OCCLUSION_TEXTURE) != 0 ? "_occlusion_tex" : "_occlusion_const";
        if ((mask & SHADER_TRANSPARENT) != 0)
            name += "_forward_blend";
        return name;
    }

    std::string FragmentShaderSource(uint32_t featureMask, bool transparent)
    {
        const bool baseTexture = (featureMask & SHADER_BASE_COLOR_TEXTURE) != 0;
        const bool mrTexture = (featureMask & SHADER_METALLIC_ROUGHNESS_TEXTURE) != 0;
        const bool normalTexture = (featureMask & SHADER_NORMAL_TEXTURE) != 0;
        const bool occlusionTexture = (featureMask & SHADER_OCCLUSION_TEXTURE) != 0;
        int binding = 0;
        const int baseBinding = baseTexture ? binding++ : -1;
        const int mrBinding = mrTexture ? binding++ : -1;
        const int normalBinding = normalTexture ? binding++ : -1;
        const int occlusionBinding = occlusionTexture ? binding++ : -1;

        std::string source = transparent ? R"(#version 450
#extension GL_GOOGLE_include_directive : enable
#include "camera.glsl"
#include "light.glsl"
#include "shadow.glsl"
#include "forward_pbr.glsl"
layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) vec4 materialFactors;
    layout(offset = 96) vec4 emissiveAlpha;
    layout(offset = 112) uvec4 forwardInfo;
} constants;
layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in mat3 vTBN;
layout(location = 0) out vec4 outColor;
)" : R"(#version 450
layout(push_constant) uniform PushConstants {
    layout(offset = 0) mat4 model;
    layout(offset = 64) vec4 baseColorFactor;
    layout(offset = 80) vec4 materialFactors;
    layout(offset = 96) vec4 emissiveAlpha;
} constants;
layout(location = 0) in vec3 vViewPos;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in mat3 vTBN;
layout(location = 0) out vec4 outBaseColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetalRough;
layout(location = 3) out float outLinearDepth;
)";
        if (baseTexture)
            source += "layout(set = 1, binding = " + std::to_string(baseBinding) +
                      ") uniform sampler2D baseColorTexture;\n";
        if (mrTexture)
            source += "layout(set = 1, binding = " + std::to_string(mrBinding) +
                      ") uniform sampler2D metallicRoughnessTexture;\n";
        if (normalTexture)
            source += "layout(set = 1, binding = " + std::to_string(normalBinding) +
                      ") uniform sampler2D normalTexture;\n";
        if (occlusionTexture)
            source += "layout(set = 1, binding = " + std::to_string(occlusionBinding) +
                      ") uniform sampler2D occlusionTexture;\n";
        source += "void main() {\n";
        source += "    vec4 baseColor = constants.baseColorFactor;\n";
        if (baseTexture)
            source += "    baseColor *= texture(baseColorTexture, vTexCoord);\n";
        if (!transparent)
            source += "    if (baseColor.a < constants.emissiveAlpha.w) discard;\n";
        source += "    float metallic = constants.materialFactors.x;\n";
        source += "    float roughness = constants.materialFactors.y;\n";
        if (mrTexture)
        {
            source += "    vec4 mr = texture(metallicRoughnessTexture, vTexCoord);\n";
            source += "    metallic *= mr.b;\n";
            source += "    roughness *= mr.g;\n";
        }
        source += "    float occlusion = 1.0;\n";
        if (occlusionTexture)
            source += "    occlusion = mix(1.0, texture(occlusionTexture, vTexCoord).r, constants.materialFactors.w);\n";
        source += "    vec3 normal = normalize(vTBN[2]);\n";
        if (normalTexture)
        {
            source += "    vec3 sampledNormal = texture(normalTexture, vTexCoord).xyz * 2.0 - 1.0;\n";
            source += "    sampledNormal.xy *= constants.materialFactors.z;\n";
            source += "    normal = normalize(vTBN * sampledNormal);\n";
        }
        if (transparent)
            source += R"(    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.04, 1.0);
    vec3 color = EvaluateForwardPBR(baseColor.rgb, metallic, roughness, occlusion,
                                    normal, vViewPos, constants.forwardInfo.x,
                                    constants.forwardInfo.yz);
    color += constants.emissiveAlpha.rgb;
    if (constants.forwardInfo.w == 1u)
        color *= baseColor.a;
    outColor = vec4(color, baseColor.a);
}
)";
        else
            source += R"(    outBaseColor = vec4(baseColor.rgb, occlusion);
    outNormal = vec4(normal * 0.5 + 0.5, 0.0);
    outMetalRough = vec4(clamp(metallic, 0.0, 1.0), clamp(roughness, 0.04, 1.0), 0.0, 0.0);
    outLinearDepth = -vViewPos.z;
}
)";
        return source;
    }

    bool CompileShader(const fs::path &source, const fs::path &output)
    {
        VKE_LOG_INFO("Compiling shader {}", source.filename().string())
        const std::string command = "glslc --target-env=vulkan1.4 -I \"builtin_assets/shader\" \"" +
                                    source.string() + "\" -o \"" + output.string() + "\"";
        if (std::system(command.c_str()) != 0)
        {
            LogError("glslc failed for " + source.string());
            return false;
        }
        return true;
    }
}

bool Converter::ExportPrimitive(int meshIndex, int primitiveIndex, PrimitiveAsset &out)
{
    const auto key = std::make_pair(meshIndex, primitiveIndex);
    auto existing = primitiveAssets.find(key);
    if (existing != primitiveAssets.end())
    {
        out = existing->second;
        return true;
    }
    if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size()))
        return false;
    const tinygltf::Mesh &mesh = model.meshes[meshIndex];
    if (primitiveIndex < 0 || primitiveIndex >= static_cast<int>(mesh.primitives.size()))
        return false;
    const tinygltf::Primitive &primitive = mesh.primitives[primitiveIndex];

    std::vector<glm::vec3> positions;
    if (!ReadAttribute(model, primitive, "POSITION", 3, positions) || positions.empty())
    {
        LogError("mesh " + std::to_string(meshIndex) + " primitive " +
                 std::to_string(primitiveIndex) + " has no valid POSITION accessor");
        return false;
    }
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
    std::vector<glm::vec2> texCoords;
    const bool hasNormals = ReadAttribute(model, primitive, "NORMAL", 3, normals) &&
                            normals.size() == positions.size();
    const bool hasTangents = ReadAttribute(model, primitive, "TANGENT", 4, tangents) &&
                             tangents.size() == positions.size();
    const bool hasTexCoords = ReadAttribute(model, primitive, "TEXCOORD_0", 2, texCoords) &&
                              texCoords.size() == positions.size();

    std::vector<uint32_t> sourceIndices;
    std::vector<uint32_t> indices;
    if (!ReadIndices(model, primitive, positions.size(), sourceIndices) ||
        !Triangulate(primitive.mode, sourceIndices, indices))
    {
        LogError("only triangle, triangle strip and triangle fan primitives are supported");
        return false;
    }

    std::vector<vke_render::Vertex> vertices(positions.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i].pos = positions[i];
        vertices[i].normal = hasNormals ? normals[i] : glm::vec3(0.0f);
        vertices[i].tangent = hasTangents ? tangents[i] : glm::vec4(0.0f);
        vertices[i].texCoord = hasTexCoords ? texCoords[i] : glm::vec2(0.0f);
    }
    if (!hasNormals)
        GenerateNormals(vertices, indices);
    if (!hasTangents)
        GenerateTangents(vertices, indices);

    const std::string meshName = SafeName(
        baseName + "_" + (mesh.name.empty() ? "mesh" + std::to_string(meshIndex) : mesh.name) +
        "_p" + std::to_string(primitiveIndex));
    const fs::path meshPath = outputDir / (meshName + ".mesh");
    std::ofstream meshOutput(meshPath, std::ios::binary);
    if (!meshOutput)
    {
        LogError("cannot write " + meshPath.string());
        return false;
    }
    std::vector<vke_render::MeshInfo> infos{
        vke_render::MeshInfo(0, vertices.size() * sizeof(vke_render::Vertex), vertices.size(),
                             0, indices.size() * sizeof(uint32_t), indices.size())};
    vke_render::Mesh::MeshDataToBinary<vke_render::Vertex, uint32_t>(
        meshOutput, infos, std::span<const vke_render::Vertex>(vertices),
        std::span<const uint32_t>(indices));
    if (!meshOutput.good())
        return false;

    out.mesh = ids.mesh++;
    if (!meshOnly)
    {
        assets.push_back({{"type", ASSET_MESH},
                          {"id", out.mesh},
                          {"name", meshName},
                          {"path", PathString(meshPath)}});
        if (!ExportMaterial(primitive.material, out.material))
            return false;
    }
    if (primitiveAssets.empty() || (primitiveAssets.size() + 1) % 100 == 0)
        VKE_LOG_INFO("Exported {} mesh primitives", primitiveAssets.size() + 1)
    primitiveAssets[key] = out;
    return true;
}

    bool Converter::ExportTexture(int textureIndex, bool srgb, uint64_t &out)
{
    const auto key = std::make_pair(textureIndex, srgb);
    auto existing = textureAssets.find(key);
    if (existing != textureAssets.end())
    {
        out = existing->second;
        return true;
    }
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
        return false;
    const tinygltf::Texture &texture = model.textures[textureIndex];
    if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size()))
        return false;
    const tinygltf::Image &image = model.images[texture.source];

    fs::path texturePath;
    if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0)
    {
        const fs::path sourcePath = resourceDir / fs::path(image.uri);
        texturePath = outputDir /
                      (SafeName(baseName + "_image_" + std::to_string(texture.source)) +
                       sourcePath.extension().string());
        std::error_code error;
        if (!fs::equivalent(sourcePath, texturePath, error))
        {
            error.clear();
            fs::copy_file(sourcePath, texturePath, fs::copy_options::overwrite_existing, error);
            if (error)
            {
                LogError("cannot copy texture " + sourcePath.string() + ": " + error.message());
                return false;
            }
        }
    }
    else if (image.bufferView >= 0)
    {
        const tinygltf::BufferView &view = model.bufferViews[image.bufferView];
        const tinygltf::Buffer &buffer = model.buffers[view.buffer];
        if (view.byteOffset + view.byteLength > buffer.data.size())
            return false;
        texturePath = outputDir / (SafeName(baseName + "_image_" +
                                             std::to_string(texture.source)) +
                                   ImageExtension(image));
        std::ofstream output(texturePath, std::ios::binary);
        output.write(reinterpret_cast<const char *>(buffer.data.data() + view.byteOffset),
                     static_cast<std::streamsize>(view.byteLength));
        if (!output.good())
            return false;
    }
    else
    {
        LogError("image " + std::to_string(texture.source) +
                 " is decoded-only; use an external URI or bufferView-backed GLB image");
        return false;
    }

    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (texture.sampler >= 0 && texture.sampler < static_cast<int>(model.samplers.size()))
    {
        const tinygltf::Sampler &sampler = model.samplers[texture.sampler];
        minFilter = TextureFilter(sampler.minFilter);
        magFilter = TextureFilter(sampler.magFilter);
        addressMode = TextureAddressMode(sampler.wrapS);
        if (sampler.wrapS != sampler.wrapT)
            std::cerr << "gltf_scene_conv: texture " << textureIndex
                      << " uses different S/T wrapping; engine texture assets use one address mode\n";
    }

    out = ids.texture++;
    const std::string textureName =
        SafeName(baseName + "_texture_" + std::to_string(textureIndex) +
                 (srgb ? "_srgb" : "_linear"));
    assets.push_back({{"type", ASSET_TEXTURE},
                      {"id", out},
                      {"name", textureName},
                      {"path", PathString(texturePath)},
                      {"format", srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM},
                      {"minFilter", minFilter},
                      {"magFilter", magFilter},
                      {"addressMode", addressMode},
                      {"anisotropy", true},
                      {"genMipMap", true}});
    textureAssets[key] = out;
    if (textureAssets.size() == 1 || textureAssets.size() % 50 == 0)
        VKE_LOG_INFO("Exported {} texture assets", textureAssets.size())
    return true;
}

bool Converter::ExportShader(int materialIndex, uint64_t &out)
{
    const tinygltf::Material defaultMaterial;
    const tinygltf::Material &material =
        materialIndex >= 0 ? model.materials[materialIndex] : defaultMaterial;
    const bool transparent = material.alphaMode == "BLEND";
    const uint32_t featureMask = ShaderFeatureMask(material) |
                                 (transparent ? SHADER_TRANSPARENT : 0u);
    auto existing = shaderAssets.find(featureMask);
    if (existing != shaderAssets.end())
    {
        out = existing->second;
        return true;
    }

    const std::string shaderName =
        SafeName(baseName + "_" + ShaderFeatureName(featureMask));
    const fs::path fragPath = outputDir / (shaderName + ".frag");
    const fs::path fragSpvPath = outputDir / (shaderName + "_frag.spv");

    if (!commonVertexShaderGenerated)
    {
        commonVertexSourcePath = outputDir / (SafeName(baseName) + "_pbr.vert");
        commonVertexBinaryPath = outputDir / (SafeName(baseName) + "_pbr_vert.spv");
        if (!WriteText(commonVertexSourcePath, VertexShaderSource()) ||
            !CompileShader(commonVertexSourcePath, commonVertexBinaryPath))
            return false;
        commonVertexShaderGenerated = true;
    }

    if (!WriteText(fragPath, FragmentShaderSource(featureMask, transparent)) ||
        !CompileShader(fragPath, fragSpvPath))
        return false;

    out = ids.shader++;
    assets.push_back({{"type", ASSET_VF_SHADER},
                      {"id", out},
                      {"name", shaderName},
                      {"path", PathString(commonVertexBinaryPath)},
                      {"fragPath", PathString(fragSpvPath)},
                      {"sourcePath", PathString(commonVertexSourcePath)},
                      {"fragSourcePath", PathString(fragPath)}});
    shaderAssets[featureMask] = out;
    VKE_LOG_INFO("Generated shader variant {} (mask 0x{:X})", shaderName, featureMask)
    return true;
}

bool Converter::ExportMaterial(int materialIndex, uint64_t &out)
{
    auto existing = materialAssets.find(materialIndex);
    if (existing != materialAssets.end())
    {
        out = existing->second;
        return true;
    }
    if (materialIndex >= static_cast<int>(model.materials.size()))
        return false;
    const tinygltf::Material defaultMaterial;
    const tinygltf::Material &material =
        materialIndex >= 0 ? model.materials[materialIndex] : defaultMaterial;

    uint64_t shader = 0;
    if (!ExportShader(materialIndex, shader))
        return false;

    json textures = json::array();
    json bindings = json::array();
    int binding = 0;
    const auto addTexture = [&](int textureIndex, bool srgb) -> bool
    {
        if (textureIndex < 0)
            return true;
        uint64_t textureAsset = 0;
        if (!ExportTexture(textureIndex, srgb, textureAsset))
            return false;
        textures.push_back(textureAsset);
        bindings.push_back({{"binding", binding++}, {"offset", 0}, {"cnt", 1}});
        return true;
    };
    if (!addTexture(material.pbrMetallicRoughness.baseColorTexture.index, true) ||
        !addTexture(material.pbrMetallicRoughness.metallicRoughnessTexture.index, false) ||
        !addTexture(material.normalTexture.index, false) ||
        !addTexture(material.occlusionTexture.index, false))
        return false;

    std::array<double, 4> baseColor{1.0, 1.0, 1.0, 1.0};
    for (size_t i = 0;
         i < std::min<size_t>(4, material.pbrMetallicRoughness.baseColorFactor.size()); ++i)
        baseColor[i] = material.pbrMetallicRoughness.baseColorFactor[i];
    std::array<double, 3> emissive{0.0, 0.0, 0.0};
    for (size_t i = 0; i < std::min<size_t>(3, material.emissiveFactor.size()); ++i)
        emissive[i] = material.emissiveFactor[i];
    const double alphaCutoff = material.alphaMode == "MASK" ? material.alphaCutoff : 0.0;
    const std::string renderMode = material.alphaMode == "MASK" ? "cutoff" :
                                   material.alphaMode == "BLEND" ? "blend" : "opaque";

    out = ids.material++;
    const std::string materialName =
        SafeName(baseName + "_" +
                 (material.name.empty() ? "material_" +
                                              (materialIndex >= 0 ? std::to_string(materialIndex)
                                                                  : std::string("default"))
                                        : material.name));
    assets.push_back({
        {"type", ASSET_MATERIAL},
        {"id", out},
        {"name", materialName},
        {"path", ""},
        {"shader", shader},
        {"renderMode", renderMode},
        {"blendMode", "alpha"},
        {"textures", textures},
        {"bindingInfos", bindings},
        {"pushConstantInfos",
         json::array({
             {{"name", "baseColorFactor"},
              {"offset", 64},
              {"component_cnt", 4},
              {"component_type", "float"},
              {"data", {baseColor[0], baseColor[1], baseColor[2], baseColor[3]}}},
             {{"name", "materialFactors"},
              {"offset", 80},
              {"component_cnt", 4},
              {"component_type", "float"},
              {"data",
               {material.pbrMetallicRoughness.metallicFactor,
                material.pbrMetallicRoughness.roughnessFactor,
                material.normalTexture.scale, material.occlusionTexture.strength}}},
             {{"name", "emissiveAlpha"},
              {"offset", 96},
              {"component_cnt", 4},
              {"component_type", "float"},
              {"data", {emissive[0], emissive[1], emissive[2], alphaCutoff}}}
         })}});
    materialAssets[materialIndex] = out;
    if (materialAssets.size() == 1 || materialAssets.size() % 50 == 0)
        VKE_LOG_INFO("Exported {} material assets", materialAssets.size())
    return true;
}

json Converter::MakeObject(uint32_t id, const std::string &name, uint32_t parent,
                           const TransformData &transform, json components, json children) const
{
    return {
        {"id", id},
        {"static", true},
        {"name", name},
        {"parent", parent},
        {"transform", TransformJSON(transform)},
        {"components", std::move(components)},
        {"children", std::move(children)}};
}

bool Converter::WriteScene(size_t sceneIndex)
{
    const tinygltf::Scene &gltfScene = model.scenes[sceneIndex];
    const std::string sourceSceneName =
        gltfScene.name.empty() ? "scene_" + std::to_string(sceneIndex) : gltfScene.name;
    VKE_LOG_INFO("Exporting scene {} ({}/{})", sourceSceneName, sceneIndex + 1,
                 model.scenes.size())
    std::set<int> reachable;
    std::vector<int> order;
    const auto visit = [&](const auto &self, int nodeIndex) -> void
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()) ||
            !reachable.insert(nodeIndex).second)
            return;
        order.push_back(nodeIndex);
        for (int child : model.nodes[nodeIndex].children)
            self(self, child);
    };
    for (int root : gltfScene.nodes)
        visit(visit, root);

    std::unordered_map<int, uint32_t> nodeIds;
    uint32_t nextObjectId = 2;
    for (int nodeIndex : order)
        nodeIds[nodeIndex] = nextObjectId++;

    std::unordered_map<int, uint32_t> parents;
    for (int nodeIndex : order)
        for (int child : model.nodes[nodeIndex].children)
            if (reachable.count(child))
                parents[child] = nodeIds[nodeIndex];

    json objects = json::array();
    objects.push_back({
        {"id", 1},
        {"static", true},
        {"name", "scene_cam"},
        {"parent", 0},
        {"transform",
         {{"pos", {0.0, 0.0, 3.0}},
          {"scl", {1.0, 1.0, 1.0}},
          {"rot", {0.0, 0.0, 0.0, 1.0}}}},
        {"components",
         json::array({
             {{"type", "camera"},
              {"id", 1},
              {"fov", 70.0},
              {"width", 1920.0},
              {"height", 1080.0},
              {"near", 0.01},
              {"far", 1000.0}},
             {{"type", "script"},
              {"className", "TestProj.TestCameraScript"},
              {"data",
               {{"JumpSpeed", 5.0},
                {"MoveSpeed", 4.0},
                {"RotateSpeed", 2.0}}}}
         })},
        {"children", json::array()}});

    for (int nodeIndex : order)
    {
        const tinygltf::Node &node = model.nodes[nodeIndex];
        json components = json::array();
        json children = json::array();
        for (int child : node.children)
            if (reachable.count(child))
                children.push_back(nodeIds[child]);

        std::vector<PrimitiveAsset> meshPrimitives;
        if (node.mesh >= 0)
        {
            const tinygltf::Mesh &mesh = model.meshes[node.mesh];
            for (int primitiveIndex = 0;
                 primitiveIndex < static_cast<int>(mesh.primitives.size()); ++primitiveIndex)
            {
                PrimitiveAsset primitiveAsset;
                if (!ExportPrimitive(node.mesh, primitiveIndex, primitiveAsset))
                    return false;
                meshPrimitives.push_back(primitiveAsset);
            }
            if (meshPrimitives.size() == 1)
                components.push_back({{"type", "renderableObject"},
                                      {"material", meshPrimitives[0].material},
                                      {"mesh", meshPrimitives[0].mesh},
                                      {"castsShadow", true}});
        }

        if (node.light >= 0 && node.light < static_cast<int>(model.lights.size()))
        {
            const tinygltf::Light &light = model.lights[node.light];
            const json color = light.color.size() == 3
                                   ? json::array({light.color[0], light.color[1], light.color[2]})
                                   : json::array({1.0, 1.0, 1.0});
            if (light.type == "directional")
                components.push_back({{"type", "directionalLight"},
                                      {"color", color},
                                      {"intensity", light.intensity}});
            else if (light.type == "point")
                components.push_back({{"type", "pointLight"},
                                      {"radius", light.range > 0.0 ? light.range : 10.0},
                                      {"color", color},
                                      {"intensity", light.intensity}});
            else if (light.type == "spot")
                components.push_back({
                    {"type", "spotLight"},
                    {"radius", light.range > 0.0 ? light.range : 10.0},
                    {"color", color},
                    {"intensity", light.intensity},
                    {"innerCone", glm::degrees(static_cast<float>(light.spot.innerConeAngle))},
                    {"outerCone", glm::degrees(static_cast<float>(light.spot.outerConeAngle))}});
        }

        if (meshPrimitives.size() > 1)
        {
            for (size_t primitiveIndex = 0; primitiveIndex < meshPrimitives.size(); ++primitiveIndex)
            {
                const uint32_t childId = nextObjectId++;
                children.push_back(childId);
                objects.push_back(MakeObject(
                    childId,
                    (node.name.empty() ? "node_" + std::to_string(nodeIndex) : node.name) +
                        "_primitive_" + std::to_string(primitiveIndex),
                    nodeIds[nodeIndex], TransformData{},
                    json::array({{{"type", "renderableObject"},
                                  {"material", meshPrimitives[primitiveIndex].material},
                                  {"mesh", meshPrimitives[primitiveIndex].mesh},
                                  {"castsShadow", true}}}),
                    json::array()));
            }
        }

        objects.push_back(MakeObject(
            nodeIds[nodeIndex],
            node.name.empty() ? "node_" + std::to_string(nodeIndex) : node.name,
            parents.count(nodeIndex) ? parents[nodeIndex] : 0,
            NodeTransform(node), std::move(components), std::move(children)));
    }

    std::sort(objects.begin(), objects.end(), [](const json &a, const json &b)
              { return a["id"].get<uint32_t>() < b["id"].get<uint32_t>(); });
    const std::string sceneName =
        SafeName(baseName + "_" +
                 (gltfScene.name.empty() ? "scene_" + std::to_string(sceneIndex) : gltfScene.name));
    const fs::path scenePath = outputDir / (sceneName + ".json");
    const json sceneJSON = {
        {"maxid", nextObjectId},
        {"objects", std::move(objects)}};
    if (!WriteJSON(scenePath, sceneJSON))
        return false;
    assets.push_back({{"type", ASSET_SCENE},
                      {"id", ids.scene++},
                      {"name", sceneName},
                      {"path", PathString(scenePath)}});
    VKE_LOG_INFO("Exported scene {} with {} objects", sceneName, nextObjectId - 1)
    return true;
}

bool Converter::ExportScenes()
{
    for (size_t sceneIndex = 0; sceneIndex < model.scenes.size(); ++sceneIndex)
        if (!WriteScene(sceneIndex))
            return false;
    return true;
}

bool Converter::Run()
{
    VKE_LOG_INFO("Starting GLB scene conversion")
    VKE_LOG_INFO("Input: {}", inputPath.string())
    VKE_LOG_INFO("Resource directory: {}", resourceDir.string())
    VKE_LOG_INFO("Output directory: {}", outputDir.string())

    std::error_code error;
    fs::create_directories(outputDir, error);
    if (error)
    {
        LogError("cannot create output directory: " + error.message());
        return false;
    }

    tinygltf::TinyGLTF loader;
    std::string warning;
    std::string loadError;
    if (!loader.LoadBinaryFromFile(&model, &loadError, &warning, inputPath.string()))
    {
        LogError(loadError.empty() ? "failed to load GLB" : loadError);
        return false;
    }
    if (!warning.empty())
        std::cerr << "gltf_scene_conv: " << warning << '\n';
    if (model.scenes.empty())
    {
        LogError("GLB contains no scenes");
        return false;
    }
    VKE_LOG_INFO("Loaded GLB: {} scenes, {} nodes, {} meshes, {} materials, {} textures, {} lights",
                 model.scenes.size(), model.nodes.size(), model.meshes.size(),
                 model.materials.size(), model.textures.size(), model.lights.size())
    if (meshOnly)
    {
        for (int meshIndex = 0; meshIndex < static_cast<int>(model.meshes.size()); ++meshIndex)
            for (int primitiveIndex = 0;
                 primitiveIndex < static_cast<int>(model.meshes[meshIndex].primitives.size()); ++primitiveIndex)
            {
                PrimitiveAsset primitiveAsset;
                if (!ExportPrimitive(meshIndex, primitiveIndex, primitiveAsset))
                    return false;
            }
        VKE_LOG_INFO("Mesh export complete: {} mesh primitives", primitiveAssets.size())
        return true;
    }
    if (!ExportScenes())
        return false;
    const fs::path assetPath = outputDir / (SafeName(baseName) + "_assets.json");
    if (!WriteJSON(assetPath, assets))
        return false;
    VKE_LOG_INFO("Conversion complete: {} mesh primitives, {} materials, {} textures, {} shader variants, {} scenes",
                 primitiveAssets.size(), materialAssets.size(), textureAssets.size(),
                 shaderAssets.size(), model.scenes.size())
    VKE_LOG_INFO("Asset description: {}", assetPath.string())
    return true;
}

int main(int argc, char **argv)
{
    const bool meshOnly = argc == 5 && std::string(argv[4]) == "--mesh-only";
    if (argc != 4 && !meshOnly)
    {
        std::cerr << "usage: gltf_scene_conv <input.glb> <resource_dir> <output_dir> [--mesh-only]\n";
        return EXIT_FAILURE;
    }
    Converter converter;
    converter.inputPath = fs::absolute(argv[1]);
    converter.resourceDir = fs::absolute(argv[2]);
    converter.outputDir = fs::absolute(argv[3]);
    converter.baseName = converter.inputPath.stem().string();
    converter.meshOnly = meshOnly;
    return converter.Run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
