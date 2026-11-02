#include <common.hpp>
#include <render/mesh.hpp>
#include <tinygltf/tiny_gltf.h>
#include <nlohmann/json.hpp>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/motion_extractor.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_animation_utils.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/raw_track.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/track_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/skeleton_utils.h>
#include <ozz/animation/runtime/track.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/memory/unique_ptr.h>
#include <ozz/base/maths/simd_math.h>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

// ---------------------------------------------------------------------------
// Command-line arguments
// ---------------------------------------------------------------------------
struct Args
{
    std::filesystem::path input;
    std::filesystem::path output;
    bool meshOnly = false;
    bool anim = false;
    bool animAll = false;
    bool rootMotion = false;
    std::string rootJointName;
};

static bool parseArgs(int argc, char **argv, Args &args)
{
    if (argc < 3)
        return false;

    args.input = argv[1];
    args.output = argv[2];

    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--mesh-only")
            args.meshOnly = true;
        else if (arg == "--anim")
            args.anim = true;
        else if (arg == "--all")
            args.animAll = true;
        else if (arg == "--root-motion")
            args.rootMotion = true;
        else if (arg == "--root-joint" && i + 1 < argc)
            args.rootJointName = argv[++i];
        else
            return false;
    }

    if (args.animAll)
        args.anim = true;

    return !args.meshOnly || (!args.anim && !args.rootMotion && args.rootJointName.empty());
}

static void usage()
{
    VKE_LOG_ERROR(
        "Usage: gltf_anim_conv <input.glb> <output_dir> [options]\n"
        "  Default outputs: <stem>.mesh, <stem>_skeleton.ozz\n"
        "  Options:\n"
        "    --mesh-only          Export only <stem>.mesh; use without animation options\n"
        "    --anim               Extract first animation\n"
        "    --all                Extract all animations (implies --anim)\n"
        "    --root-motion        Extract root motion tracks\n"
        "    --root-joint <name>  Unique root-motion joint name (defaults to the skin root)")
}

// ---------------------------------------------------------------------------
// GLTF scene validation
// ---------------------------------------------------------------------------
static bool checkAttribute(const char *name,
                           const tinygltf::Model &model,
                           const tinygltf::Primitive &prim,
                           int type, int ctype)
{
    auto it = prim.attributes.find(name);
    if (it == prim.attributes.end())
        return true;
    VKE_LOG_INFO("CHECK ATTR {}", name)
    const tinygltf::Accessor &accessor = model.accessors[it->second];
    return accessor.type == type && accessor.componentType == ctype;
}

static bool checkSkin(const tinygltf::Model &model, int meshID)
{
    auto &mesh = model.meshes[meshID];
    VKE_LOG_INFO("CHECK SKIN {} NAME {} PRIM_CNT {}", meshID, mesh.name, mesh.primitives.size())
    for (auto &prim : mesh.primitives)
    {
        if (prim.attributes.size() == 0)
            return false;
        int cnt = model.accessors[prim.attributes.begin()->second].count;
        for (auto &[name, idx] : prim.attributes)
            if (model.accessors[idx].count != cnt)
                return false;

        if (!checkAttribute("NORMAL", model, prim, TINYGLTF_TYPE_VEC3, TINYGLTF_PARAMETER_TYPE_FLOAT) ||
            !checkAttribute("POSITION", model, prim, TINYGLTF_TYPE_VEC3, TINYGLTF_PARAMETER_TYPE_FLOAT) ||
            !checkAttribute("TANGENT", model, prim, TINYGLTF_TYPE_VEC4, TINYGLTF_PARAMETER_TYPE_FLOAT) ||
            !checkAttribute("TEXCOORD_0", model, prim, TINYGLTF_TYPE_VEC2, TINYGLTF_PARAMETER_TYPE_FLOAT) ||
            !checkAttribute("JOINTS_0", model, prim, TINYGLTF_TYPE_VEC4, TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE) ||
            !checkAttribute("WEIGHTS_0", model, prim, TINYGLTF_TYPE_VEC4, TINYGLTF_PARAMETER_TYPE_FLOAT))
            return false;

        if (prim.indices == -1)
            return false;
        const tinygltf::Accessor &accessor = model.accessors[prim.indices];
        if (accessor.type != TINYGLTF_TYPE_SCALAR)
            return false;
        if (accessor.componentType != TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT &&
            accessor.componentType != TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT)
            return false;
    }
    return true;
}

static vke_common::SceneResult<int> findSkinNode(const tinygltf::Model &model, const tinygltf::Scene &scene)
{
    std::vector<bool> visited(model.nodes.size(), false);
    std::vector<int> pending(scene.nodes);
    int skinNode = -1;
    while (!pending.empty())
    {
        const int nodeID = pending.back();
        pending.pop_back();
        if (nodeID < 0 || nodeID >= model.nodes.size())
            return std::unexpected("Scene contains an invalid node index.");
        if (visited[nodeID])
            return std::unexpected("Scene hierarchy must be a forest without shared nodes or cycles.");
        visited[nodeID] = true;
        const auto &node = model.nodes[nodeID];
        pending.insert(pending.end(), node.children.begin(), node.children.end());
        if (node.skin < 0)
            continue;
        if (skinNode != -1)
            return std::unexpected("Scene contains multiple skinned mesh nodes; export one character at a time.");
        if (node.skin >= model.skins.size() || node.mesh < 0 || node.mesh >= model.meshes.size())
            return std::unexpected("Skinned node must reference a valid mesh and skin.");
        skinNode = nodeID;
    }
    if (skinNode == -1)
        return std::unexpected("Scene contains no skinned mesh node.");
    const auto &node = model.nodes[skinNode];
    const auto &skin = model.skins[node.skin];
    if (skin.joints.empty() || skin.joints.size() > 256)
        return std::unexpected("Skin must contain between 1 and 256 joints.");
    for (int joint : skin.joints)
        if (joint < 0 || joint >= visited.size() || !visited[joint])
            return std::unexpected("Every skin joint must belong to the selected scene.");
    if (skin.skeleton >= 0 && (skin.skeleton >= visited.size() || !visited[skin.skeleton]))
        return std::unexpected("Skin skeleton root must belong to the selected scene.");
    if (!checkSkin(model, node.mesh))
        return std::unexpected("Unsupported skinned mesh attributes or indices.");
    if (skin.inverseBindMatrices >= 0)
    {
        if (skin.inverseBindMatrices >= model.accessors.size())
            return std::unexpected("Invalid inverse bind matrix accessor.");
        const auto &accessor = model.accessors[skin.inverseBindMatrices];
        if (accessor.type != TINYGLTF_TYPE_MAT4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            accessor.count < skin.joints.size() || accessor.sparse.isSparse || accessor.bufferView < 0)
            return std::unexpected("Inverse bind matrices require a non-sparse float MAT4 accessor with at least one matrix per skin joint.");
    }
    return skinNode;
}

// ---------------------------------------------------------------------------
// GLTF buffer helpers
// ---------------------------------------------------------------------------
template <typename IT, typename OT, size_t OSTRIDE>
static void copyGLTFBuffer(const tinygltf::Model &model,
                           uint32_t accessorIdx,
                           vke_render::CPUBuffer<> &obuffer,
                           size_t offset, size_t count)
{
    auto &accessor = model.accessors[accessorIdx];
    auto &view = model.bufferViews[accessor.bufferView];
    auto &buffer = model.buffers[view.buffer];
    std::byte *obufferp = obuffer.data + offset;

    const unsigned char *bufferp = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    uint32_t componentCnt = tinygltf::GetNumComponentsInType(uint32_t(accessor.type));

    size_t stride = view.byteStride == 0
                        ? tinygltf::GetComponentSizeInBytes(uint32_t(accessor.componentType)) * componentCnt
                        : view.byteStride;
    for (size_t i = 0; i < count; ++i)
    {
        for (uint32_t j = 0; j < componentCnt; ++j)
            *(((OT *)obufferp) + j) = *(((IT *)bufferp) + j);
        obufferp += OSTRIDE;
        bufferp += stride;
    }
}

template <typename IT, typename OT>
static void copyAttribute(const tinygltf::Model &model,
                          const tinygltf::Primitive &prim,
                          const char *name,
                          vke_render::CPUBuffer<> &obuffer,
                          size_t offset)
{
    VKE_LOG_INFO("CPOY ATTR {}", name)
    auto it = prim.attributes.find(name);
    if (it != prim.attributes.end())
        copyGLTFBuffer<IT, OT, sizeof(vke_render::SkinVertex)>(model, it->second, obuffer, offset, model.accessors[it->second].count);
}

// ---------------------------------------------------------------------------
// Skinned-mesh processing
// ---------------------------------------------------------------------------
static void processSkinNode(const tinygltf::Model &model, const tinygltf::Node &node,
                            const std::string &opth,
                            const std::vector<int> &nodeToJoint)
{
    auto &mesh = model.meshes[node.mesh];
    size_t vertexSumSize = 0, indexSumSize = 0;
    std::vector<vke_render::MeshInfo> meshInfos(mesh.primitives.size());
    std::map<uint32_t, uint32_t> indexFirstPrim;
    int i = 0;
    for (auto &prim : mesh.primitives)
    {
        vke_render::MeshInfo &meshInfo = meshInfos[i];
        meshInfo.vertexOffset = vertexSumSize;
        meshInfo.vertexCnt = model.accessors[prim.attributes.begin()->second].count;
        meshInfo.vertexSize = sizeof(vke_render::SkinVertex) * meshInfo.vertexCnt;
        vertexSumSize += meshInfo.vertexSize;

        auto it = indexFirstPrim.find(prim.indices);

        if (it == indexFirstPrim.end())
        {
            const tinygltf::Accessor &accessor = model.accessors[prim.indices];
            meshInfo.indexCnt = accessor.count;
            meshInfo.indexOffset = indexSumSize;
            meshInfo.indexSize = tinygltf::GetComponentSizeInBytes(uint32_t(accessor.componentType)) * accessor.count;
            indexSumSize += meshInfo.indexSize;
            indexFirstPrim[prim.indices] = i;
        }
        else
        {
            vke_render::MeshInfo &prevInfo = meshInfos[it->second];
            meshInfo.indexOffset = prevInfo.indexOffset;
            meshInfo.indexSize = prevInfo.indexSize;
            meshInfo.indexCnt = prevInfo.indexCnt;
        }

        VKE_LOG_INFO("VOFF {} VSIZ {} IOFF {} ISIZ {}",
                     meshInfo.vertexOffset, meshInfo.vertexSize,
                     meshInfo.indexOffset, meshInfo.indexSize)
        ++i;
    }

    vke_render::CPUBuffer<> vertices(vertexSumSize);
    vke_render::CPUBuffer<> indices(indexSumSize);
    // Optional attributes absent from the source retain zero values.
    std::memset(vertices.data, 0, vertices.size);

    i = 0;
    for (auto &prim : mesh.primitives)
    {
        VKE_LOG_INFO("DO COPY {}", i)
        vke_render::MeshInfo &meshInfo = meshInfos[i++];
        copyAttribute<float, float>(model, prim, "POSITION", vertices, meshInfo.vertexOffset);
        copyAttribute<float, float>(model, prim, "NORMAL", vertices, meshInfo.vertexOffset + 3 * sizeof(float));
        copyAttribute<float, float>(model, prim, "TANGENT", vertices, meshInfo.vertexOffset + 6 * sizeof(float));
        copyAttribute<float, float>(model, prim, "TEXCOORD_0", vertices, meshInfo.vertexOffset + 10 * sizeof(float));
        copyAttribute<float, float>(model, prim, "WEIGHTS_0", vertices, meshInfo.vertexOffset + 12 * sizeof(float));
        copyAttribute<uint8_t, uint32_t>(model, prim, "JOINTS_0", vertices, meshInfo.vertexOffset + 16 * sizeof(float));
        if (i > 1 && meshInfo.indexOffset < meshInfos[i - 2].indexOffset)
            continue;
        VKE_LOG_INFO("COPY INDEX0 {}", meshInfo.indexOffset)
        if (meshInfo.indexSize / meshInfo.indexCnt == 2)
            copyGLTFBuffer<uint16_t, uint16_t, sizeof(uint16_t)>(model, prim.indices, indices, meshInfo.indexOffset, meshInfo.indexCnt);
        else
            copyGLTFBuffer<uint32_t, uint32_t, sizeof(uint32_t)>(model, prim.indices, indices, meshInfo.indexOffset, meshInfo.indexCnt);
        VKE_LOG_INFO("COPY INDEX1 {}", meshInfo.indexOffset)
    }
    for (auto &info : meshInfos)
        VKE_LOG_INFO("VO {} VS {} VC {} IO {} IS {} IC {}",
                     info.vertexOffset, info.vertexSize, info.vertexCnt,
                     info.indexOffset, info.indexSize, info.indexCnt)

    const auto &skin = model.skins[node.skin];
    size_t skinSize = skin.joints.size() * sizeof(glm::mat4);
    vke_render::CPUBuffer<> ibmBuffer(skinSize);
    if (skin.inverseBindMatrices >= 0)
        copyGLTFBuffer<float, float, sizeof(glm::mat4)>(
            model, skin.inverseBindMatrices, ibmBuffer, 0, skin.joints.size());
    else
        std::fill_n(reinterpret_cast<glm::mat4 *>(ibmBuffer.data), skin.joints.size(), glm::mat4(1.0f));

    std::ofstream outFile(opth, std::ios::binary);

    std::vector<int> joints;

    for (auto &jointID : skin.joints)
        joints.push_back(nodeToJoint[jointID]);

    vke_render::Mesh::MeshDataToBinary(outFile, meshInfos, vertices, indices, ibmBuffer, joints);
}

// ---------------------------------------------------------------------------
// OZZ skeleton helpers
// ---------------------------------------------------------------------------
static bool convertNodeToJoint(
    const tinygltf::Model &model,
    int nodeID,
    ozz::animation::offline::RawSkeleton::Joint *joint,
    std::vector<int> &nodeToJoint, int &jointCount)
{
    const auto &node = model.nodes[nodeID];
    // SkeletonBuilder preserves this depth-first traversal order, including scene roots.
    nodeToJoint[nodeID] = jointCount++;
    joint->name = node.name.c_str();
    joint->transform = ozz::math::Transform::identity();
    if (!node.matrix.empty())
    {
        ozz::math::Float4x4 matrix;
        for (int column = 0; column < 4; ++column)
            matrix.cols[column] = ozz::math::simd_float4::Load(
                static_cast<float>(node.matrix[column * 4]),
                static_cast<float>(node.matrix[column * 4 + 1]),
                static_cast<float>(node.matrix[column * 4 + 2]),
                static_cast<float>(node.matrix[column * 4 + 3]));
        if (!ozz::math::ToAffine(matrix, &joint->transform))
        {
            VKE_LOG_ERROR("Node {} matrix cannot be represented as a joint TRS transform.", node.name)
            return false;
        }
    }
    else
    {
        if (!node.translation.empty())
            joint->transform.translation = {
                static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2])};
        if (!node.rotation.empty())
            joint->transform.rotation = {
                static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3])};
        if (!node.scale.empty())
            joint->transform.scale = {
                static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2])};
    }
    joint->children.resize(node.children.size());

    for (size_t i = 0; i < node.children.size(); ++i)
    {
        ozz::animation::offline::RawSkeleton::Joint &childjoint =
            joint->children[i];

        if (!convertNodeToJoint(model, node.children[i], &childjoint, nodeToJoint, jointCount))
            return false;
    }

    return true;
}

static ozz::unique_ptr<ozz::animation::Skeleton> buildOzzSkeleton(
    const tinygltf::Model &model, const tinygltf::Scene &scene, std::vector<int> &nodeToJoint)
{
    ozz::animation::offline::RawSkeleton rawSkeleton;
    rawSkeleton.roots.resize(scene.nodes.size());
    int jointCount = 0;
    for (size_t i = 0; i < scene.nodes.size(); ++i)
        if (!convertNodeToJoint(model, scene.nodes[i], &rawSkeleton.roots[i], nodeToJoint, jointCount))
            return nullptr;

    if (!rawSkeleton.Validate())
    {
        VKE_LOG_ERROR("Output skeleton failed validation. This is likely an implementation issue.")
        return nullptr;
    }

    ozz::animation::offline::SkeletonBuilder builder;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton = builder(rawSkeleton);

    if (!skeleton)
    {
        VKE_LOG_ERROR("Failed to build runtime skeleton.")
        return nullptr;
    }

    return skeleton;
}

static void saveOzzSkeleton(const std::filesystem::path &output,
                            const ozz::animation::Skeleton &skeleton)
{
    ozz::io::File file(output.string().c_str(), "wb");
    VKE_FATAL_IF(!file.opened(), "Failed to open output skeleton file {}", output.string())
    ozz::io::OArchive archive(&file);
    archive << skeleton;
    VKE_LOG_INFO("Wrote skeleton {}", output.string())
}

// ---------------------------------------------------------------------------
// Animation extraction
// ---------------------------------------------------------------------------
static std::string sanitizeFileName(const std::string &name, size_t fallbackIndex)
{
    std::string sanitized;
    sanitized.reserve(name.size());

    for (char c : name)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_')
            sanitized.push_back(c);
        else if (c == ' ' || c == '.')
            sanitized.push_back('_');
    }

    if (sanitized.empty())
        sanitized = "animation_" + std::to_string(fallbackIndex);
    return sanitized;
}

static std::filesystem::path outputPathForAnimation(
    const std::filesystem::path &outputDirectory,
    const tinygltf::Animation &animation,
    size_t animationIndex,
    std::unordered_map<std::string, size_t> &nameCounts)
{
    const std::string baseName = sanitizeFileName(animation.name, animationIndex);
    size_t &count = nameCounts[baseName];
    const std::string fileName = count == 0
                                     ? baseName + ".anim"
                                     : baseName + "_" + std::to_string(count) + ".anim";
    ++count;
    return outputDirectory / fileName;
}

static const unsigned char *accessorData(const tinygltf::Model &model,
                                         int accessorIndex, size_t &stride)
{
    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    VKE_FATAL_IF(accessor.sparse.isSparse, "Sparse animation accessors are not supported yet.")
    VKE_FATAL_IF(accessor.bufferView < 0, "Animation accessor has no buffer view.")

    const tinygltf::BufferView &view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer &buffer = model.buffers[view.buffer];
    const int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    stride = view.byteStride == 0 ? componentCount * componentSize : view.byteStride;
    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
}

static std::vector<float> readFloatAccessor(const tinygltf::Model &model,
                                            int accessorIndex, int expectedType)
{
    const tinygltf::Accessor &accessor = model.accessors[accessorIndex];
    VKE_FATAL_IF(accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT, "Animation accessor must contain floats.")
    VKE_FATAL_IF(accessor.type != expectedType, "Animation accessor has unexpected component count.")

    const int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    size_t stride = 0;
    const unsigned char *data = accessorData(model, accessorIndex, stride);

    std::vector<float> out(accessor.count * componentCount);
    for (size_t i = 0; i < accessor.count; ++i)
    {
        const float *src = reinterpret_cast<const float *>(data + i * stride);
        for (int c = 0; c < componentCount; ++c)
            out[i * componentCount + c] = src[c];
    }
    return out;
}

static vke_common::SceneResult<std::vector<float>> readAnimationValues(
    const tinygltf::Model &model, const tinygltf::AnimationSampler &sampler, int type)
{
    if (sampler.interpolation != "LINEAR" && sampler.interpolation != "STEP")
        return std::unexpected("Animation interpolation " + sampler.interpolation +
                               " is unsupported. Bake the channel to LINEAR before importing.");
    auto values = readFloatAccessor(model, sampler.output, type);
    if (sampler.interpolation == "STEP")
    {
        // Collapse exporter noise to a constant track: 1e-5 absolute tolerance
        // plus 1e-5 relative to the first key's largest component.
        const size_t components = tinygltf::GetNumComponentsInType(type);
        float magnitude = 0.0f;
        for (size_t c = 0; c < std::min(components, values.size()); ++c)
            magnitude = std::max(magnitude, std::abs(values[c]));
        const float tolerance = 1e-5f * (1.0f + magnitude);
        for (size_t i = components; i < values.size(); i += components)
        {
            float sign = 1.0f;
            if (type == TINYGLTF_TYPE_VEC4)
            {
                // Opposite quaternion signs represent the same rotation.
                float dot = 0.0f;
                for (size_t c = 0; c < components; ++c)
                    dot += values[c] * values[i + c];
                if (dot < 0.0f)
                    sign = -1.0f;
            }
            for (size_t c = 0; c < components; ++c)
            {
                if (!(std::abs(sign * values[i + c] - values[c]) <= tolerance))
                    return std::unexpected("Non-constant STEP animation is unsupported. Bake the channel to LINEAR before importing.");
                values[i + c] = values[c];
            }
        }
    }
    return values;
}

static vke_common::SceneResult<ozz::animation::offline::RawAnimation> buildRawAnimation(
    const tinygltf::Model &model,
    const tinygltf::Animation &gltfAnimation,
    const ozz::animation::Skeleton &skeleton,
    const std::vector<int> &nodeToJoint)
{
    ozz::animation::offline::RawAnimation raw;
    raw.name = gltfAnimation.name.c_str();
    raw.tracks.resize(skeleton.num_joints());
    raw.duration = 0.0f;

    for (const tinygltf::AnimationChannel &channel : gltfAnimation.channels)
    {
        if (channel.target_node < 0 || channel.target_node >= nodeToJoint.size())
            return std::unexpected("Animation channel references an invalid node.");
        const int joint = nodeToJoint[channel.target_node];
        if (joint == -1)
        {
            VKE_LOG_INFO("Skipping animation channel for node {} outside the selected scene", channel.target_node)
            continue;
        }

        const tinygltf::AnimationSampler &sampler = gltfAnimation.samplers[channel.sampler];
        const std::vector<float> times = readFloatAccessor(model, sampler.input, TINYGLTF_TYPE_SCALAR);
        if (!times.empty())
            raw.duration = std::max(raw.duration, times.back());
        ozz::animation::offline::RawAnimation::JointTrack &track = raw.tracks[joint];

        if (channel.target_path != "translation" && channel.target_path != "rotation" && channel.target_path != "scale")
        {
            VKE_LOG_INFO("Skipping unsupported animation target path {}", channel.target_path)
            continue;
        }
        const int type = channel.target_path == "rotation" ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3;
        auto result = readAnimationValues(model, sampler, type);
        if (!result)
            return std::unexpected(result.error());
        const auto &values = *result;
        const size_t components = tinygltf::GetNumComponentsInType(type);
        if (values.size() != times.size() * components)
            return std::unexpected(channel.target_path + " key count mismatch.");
        for (size_t i = 0; i < times.size(); ++i)
        {
            if (channel.target_path == "translation")
                track.translations.push_back({times[i], {values[i * 3 + 0], values[i * 3 + 1], values[i * 3 + 2]}});
            else if (channel.target_path == "rotation")
                track.rotations.push_back({times[i], {values[i * 4 + 0], values[i * 4 + 1], values[i * 4 + 2], values[i * 4 + 3]}});
            else
                track.scales.push_back({times[i], {values[i * 3 + 0], values[i * 3 + 1], values[i * 3 + 2]}});
        }
    }

    // An absent channel retains the node's local transform in every frame.
    for (int joint = 0; joint < skeleton.num_joints(); ++joint)
    {
        const auto rest = ozz::animation::GetJointLocalRestPose(skeleton, joint);
        auto &track = raw.tracks[joint];
        if (track.translations.empty())
            track.translations.push_back({0.0f, rest.translation});
        if (track.rotations.empty())
            track.rotations.push_back({0.0f, rest.rotation});
        if (track.scales.empty())
            track.scales.push_back({0.0f, rest.scale});
    }

    if (raw.duration <= 0.0f)
        return std::unexpected("Animation duration must be positive.");
    if (!raw.Validate())
        return std::unexpected("Raw animation failed validation.");
    return raw;
}

static vke_common::SceneResult<int> rootJointIndex(const ozz::animation::Skeleton &skeleton,
                                                 const tinygltf::Skin &skin,
                                                 const std::vector<int> &nodeToJoint,
                                                 const std::string &rootJointName)
{
    const auto parents = skeleton.joint_parents();
    auto isAncestor = [parents](int ancestor, int joint)
    {
        for (; joint != ozz::animation::Skeleton::kNoParent; joint = parents[joint])
            if (joint == ancestor)
                return true;
        return false;
    };
    int root = -1;
    if (!rootJointName.empty())
    {
        const auto names = skeleton.joint_names();
        for (int i = 0; i < names.size(); ++i)
            if (rootJointName == names[i])
            {
                if (root != -1)
                    return std::unexpected("Root-motion joint name is ambiguous: " + rootJointName);
                root = i;
            }
        if (root == -1)
            return std::unexpected("Root-motion joint does not exist: " + rootJointName);
    }
    else if (skin.skeleton >= 0)
        root = nodeToJoint[skin.skeleton];
    else
    {
        root = nodeToJoint[skin.joints.front()];
        for (int joint : skin.joints)
            while (root != -1 && !isAncestor(root, nodeToJoint[joint]))
                root = parents[root];
        if (root == -1)
            return std::unexpected("Root motion requires a common ancestor of all skin joints.");
        return root;
    }
    for (int joint : skin.joints)
        if (!isAncestor(root, nodeToJoint[joint]))
            return std::unexpected("Root-motion joint must be an ancestor of every skin joint.");
    return root;
}

static vke_common::SceneResult<void> extractRootMotion(
    const ozz::animation::offline::RawAnimation &raw,
    const ozz::animation::Skeleton &skeleton, int rootJoint,
    ozz::animation::offline::RawFloat3Track &position,
    ozz::animation::offline::RawQuaternionTrack &rotation,
    ozz::animation::offline::RawAnimation &baked)
{
    using namespace ozz::math;
    using namespace ozz::animation::offline;
    Transform parent = Transform::identity();
    const auto parents = skeleton.joint_parents();
    for (int ancestor = parents[rootJoint]; ancestor != ozz::animation::Skeleton::kNoParent; ancestor = parents[ancestor])
    {
        // Missing channels have already been populated from the rest pose.
        const auto &track = raw.tracks[ancestor];
        const Transform local{track.translations.front().value, track.rotations.front().value, track.scales.front().value};
        const bool constantPosition = std::all_of(track.translations.begin(), track.translations.end(),
                                                 [&](const auto &key) { return key.value == local.translation; });
        const bool constantRotation = std::all_of(track.rotations.begin(), track.rotations.end(),
                                                 [&](const auto &key) { return key.value == local.rotation || key.value == -local.rotation; });
        const bool constantScale = std::all_of(track.scales.begin(), track.scales.end(),
                                              [&](const auto &key) { return key.value == local.scale; });
        if (!constantPosition || !constantRotation || !constantScale)
            return std::unexpected("Root-motion ancestors must be constant throughout the clip. Bake ancestor animation into the selected root joint.");
        if (local.scale.x <= 0.0f || !Compare(local.scale, Float3(local.scale.x), 1e-5f * local.scale.x))
            return std::unexpected("Root-motion ancestors require positive uniform scale. Bake non-uniform or reflected scale before importing.");
        parent.translation = local.translation + TransformVector(local.rotation, parent.translation * local.scale.x);
        parent.rotation = local.rotation * parent.rotation;
        parent.scale = parent.scale * local.scale.x;
    }

    // MotionExtractor operates in its root track's space. A single model-space
    // joint makes its XZ translation and yaw directly consumable by the entity.
    RawSkeleton modelSkeleton;
    modelSkeleton.roots.resize(1);
    auto &rest = modelSkeleton.roots[0].transform;
    rest = ozz::animation::GetJointLocalRestPose(skeleton, rootJoint);
    rest.translation = parent.translation + TransformVector(parent.rotation, rest.translation * parent.scale.x);
    rest.rotation = parent.rotation * rest.rotation;
    rest.scale = rest.scale * parent.scale.x;
    SkeletonBuilder skeletonBuilder;
    const auto extractionSkeleton = skeletonBuilder(modelSkeleton);

    RawAnimation modelAnimation;
    modelAnimation.duration = raw.duration;
    modelAnimation.tracks.push_back(raw.tracks[rootJoint]);
    auto &modelTrack = modelAnimation.tracks[0];
    for (auto &key : modelTrack.translations)
        key.value = parent.translation + TransformVector(parent.rotation, key.value * parent.scale.x);
    for (auto &key : modelTrack.rotations)
        key.value = parent.rotation * key.value;
    for (auto &key : modelTrack.scales)
        key.value = key.value * parent.scale.x;

    // Removing yaw curves the baked translation, even when the source has no
    // translation channel. Sample all components together and retain source key
    // times so rotation-only tracks and short source segments are represented.
    std::vector<float> sampleTimes{0.0f, raw.duration};
    for (const auto &key : modelTrack.translations)
        sampleTimes.push_back(key.time);
    for (const auto &key : modelTrack.rotations)
        sampleTimes.push_back(key.time);
    for (const auto &key : modelTrack.scales)
        sampleTimes.push_back(key.time);
    const FixedRateSamplingTime samplingTime(raw.duration, 60.0f);
    for (size_t frame = 1; frame + 1 < samplingTime.num_keys(); ++frame)
        sampleTimes.push_back(samplingTime.time(frame));
    std::sort(sampleTimes.begin(), sampleTimes.end());
    // Motion tracks store normalized float ratios, which must remain distinct
    // after conversion from seconds. Preserve the exact clip endpoint.
    sampleTimes.erase(std::unique(sampleTimes.begin(), sampleTimes.end(),
                                  [duration = raw.duration](float a, float b) { return a / duration == b / duration; }),
                      sampleTimes.end());
    sampleTimes.back() = raw.duration;
    // Raw animation validation established sorted, non-empty component tracks.
    auto sampleComponent = [](const auto &keys, float time, auto interpolate)
    {
        if (time <= keys.front().time)
            return keys.front().value;
        if (time >= keys.back().time)
            return keys.back().value;
        const auto right = std::lower_bound(keys.begin(), keys.end(), time,
                                            [](const auto &key, float t) { return key.time < t; });
        const auto &left = *(right - 1);
        return interpolate(left.value, right->value, (time - left.time) / (right->time - left.time));
    };
    RawAnimation::JointTrack sampledTrack;
    sampledTrack.translations.reserve(sampleTimes.size());
    sampledTrack.rotations.reserve(sampleTimes.size());
    sampledTrack.scales.reserve(sampleTimes.size());
    for (float time : sampleTimes)
    {
        sampledTrack.translations.push_back({time, sampleComponent(modelTrack.translations, time, LerpTranslation)});
        sampledTrack.rotations.push_back({time, sampleComponent(modelTrack.rotations, time, LerpRotation)});
        sampledTrack.scales.push_back({time, sampleComponent(modelTrack.scales, time, LerpScale)});
    }
    modelTrack = std::move(sampledTrack);

    MotionExtractor extractor;
    RawAnimation modelBaked;
    if (!extractor(modelAnimation, *extractionSkeleton, &position, &rotation, &modelBaked))
        return std::unexpected("Failed to extract model-space root motion.");

    // Restore the selected joint's parent space. All skin joints descend from
    // it, so applying the extracted entity motion reconstructs the whole skin.
    baked = raw;
    auto &localTrack = baked.tracks[rootJoint];
    localTrack = std::move(modelBaked.tracks[0]);
    const auto inverseRotation = Conjugate(parent.rotation);
    for (auto &key : localTrack.translations)
        key.value = TransformVector(inverseRotation, key.value - parent.translation) / parent.scale.x;
    for (auto &key : localTrack.rotations)
        key.value = inverseRotation * key.value;
    for (auto &key : localTrack.scales)
        key.value = key.value / parent.scale.x;
    return {};
}

static vke_common::SceneResult<void> saveAnimation(
    const std::filesystem::path &output,
    const ozz::animation::offline::RawAnimation &raw,
    const ozz::animation::Skeleton &skeleton,
    bool rootMotion,
    int rootJoint)
{
    ozz::animation::offline::RawAnimation rawForBuild;
    const ozz::animation::offline::RawAnimation *animationInput = &raw;
    ozz::animation::offline::RawFloat3Track rawMotionPosition;
    ozz::animation::offline::RawQuaternionTrack rawMotionRotation;

    if (rootMotion)
    {
        if (auto result = extractRootMotion(raw, skeleton, rootJoint, rawMotionPosition, rawMotionRotation, rawForBuild); !result)
            return result;
        animationInput = &rawForBuild;
    }

    ozz::animation::offline::AnimationBuilder animationBuilder;
    ozz::unique_ptr<ozz::animation::Animation> animation = animationBuilder(*animationInput);
    VKE_FATAL_IF(!animation, "Failed to build runtime animation.")

    ozz::io::File file(output.string().c_str(), "wb");
    VKE_FATAL_IF(!file.opened(), "Failed to open output animation file {}", output.string())
    ozz::io::OArchive archive(&file);
    archive << *animation;

    if (rootMotion)
    {
        ozz::animation::offline::TrackBuilder trackBuilder;
        ozz::unique_ptr<ozz::animation::Float3Track> motionPosition = trackBuilder(rawMotionPosition);
        ozz::unique_ptr<ozz::animation::QuaternionTrack> motionRotation = trackBuilder(rawMotionRotation);
        VKE_FATAL_IF(!motionPosition || !motionRotation, "Failed to build root motion tracks.")
        archive << *motionPosition;
        archive << *motionRotation;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    Args args;
    if (!parseArgs(argc, argv, args))
    {
        usage();
        return -1;
    }

    VKE_LOG_INFO("PTH {} ODIR {}", args.input.string(), args.output.string())

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    if (!loader.LoadBinaryFromFile(&model, &err, &warn, args.input.string().c_str()))
    {
        VKE_LOG_ERROR("Failed to load GLTF {}: {}", args.input.string(), err)
        return -1;
    }
    if (!warn.empty())
        VKE_LOG_INFO("GLTF warning: {}", warn)

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    VKE_FATAL_IF(sceneIndex >= model.scenes.size(), "GLTF has no valid default scene.")
    const auto &scene = model.scenes[sceneIndex];
    const auto skinNode = findSkinNode(model, scene);
    VKE_FATAL_IF(!skinNode, "{}", skinNode.error())
    const auto &node = model.nodes[*skinNode];

    // Output paths
    std::filesystem::path meshPath = args.output / args.input.stem();
    meshPath += ".mesh";
    std::filesystem::path skeletonPath = args.output / (args.input.stem().string() + "_skeleton.ozz");

    // Build the skeleton to establish the mesh's joint mapping.
    std::vector<int> nodeToJoint(model.nodes.size(), -1);
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton = buildOzzSkeleton(model, scene, nodeToJoint);
    VKE_FATAL_IF(!skeleton, "Failed to build skeleton.")
    int rootJnt = -1;
    if (args.anim && args.rootMotion)
    {
        const auto root = rootJointIndex(*skeleton, model.skins[node.skin], nodeToJoint, args.rootJointName);
        if (!root)
        {
            VKE_LOG_ERROR("{}", root.error())
            return 1;
        }
        rootJnt = *root;
    }
    std::error_code ec;
    std::filesystem::create_directories(args.output, ec);
    if (ec)
    {
        VKE_LOG_ERROR("Failed to create output directory {}: {}", args.output.string(), ec.message())
        return 1;
    }
    if (!args.meshOnly)
        saveOzzSkeleton(skeletonPath, *skeleton);

    // Process skinned mesh
    processSkinNode(model, node, meshPath.string(), nodeToJoint);
    VKE_LOG_INFO("Wrote mesh {}", meshPath.string())

    // Extract animations (optional)
    if (args.anim)
    {
        VKE_FATAL_IF(model.animations.empty(), "GLTF has no animations.")
        if (args.animAll)
        {
            std::unordered_map<std::string, size_t> nameCounts;
            for (size_t i = 0; i < model.animations.size(); ++i)
            {
                const tinygltf::Animation &gltfAnimation = model.animations[i];
                const auto raw = buildRawAnimation(model, gltfAnimation, *skeleton, nodeToJoint);
                if (!raw)
                {
                    VKE_LOG_ERROR("Animation {}: {}", gltfAnimation.name, raw.error())
                    return -1;
                }
                const std::filesystem::path output =
                    outputPathForAnimation(args.output, gltfAnimation, i, nameCounts);
                const auto saved = saveAnimation(output, *raw, *skeleton, args.rootMotion, rootJnt);
                if (!saved)
                {
                    VKE_LOG_ERROR("Animation {}: {}", gltfAnimation.name, saved.error())
                    return 1;
                }
                VKE_LOG_INFO("Wrote animation {} rootMotion={}", output.string(), args.rootMotion)
            }
        }
        else
        {
            const auto raw = buildRawAnimation(model, model.animations[0], *skeleton, nodeToJoint);
            if (!raw)
            {
                VKE_LOG_ERROR("Animation {}: {}", model.animations[0].name, raw.error())
                return -1;
            }
            const std::string animName = sanitizeFileName(model.animations[0].name, 0);
            const std::filesystem::path animPath = args.output / (animName + ".anim");
            const auto saved = saveAnimation(animPath, *raw, *skeleton, args.rootMotion, rootJnt);
            if (!saved)
            {
                VKE_LOG_ERROR("Animation {}: {}", model.animations[0].name, saved.error())
                return 1;
            }
            VKE_LOG_INFO("Wrote animation {} rootMotion={}", animPath.string(), args.rootMotion)
        }
    }

    return 0;
}
