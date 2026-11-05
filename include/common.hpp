#ifndef COMMON_H
#define COMMON_H

#include <logger.hpp>
#include <filesystem>
#include <paths.hpp>
#include <expected>
#include <string>

#define VKE_EXIT(code) std::exit(code);

#define VKE_FATAL(...)                          \
    {                                           \
        vke_common::Logger::Error(__VA_ARGS__); \
        VKE_EXIT(EXIT_FAILURE)                  \
    }

#define VKE_FATAL_IF(cond, ...)    \
    {                              \
        if (cond)                  \
            VKE_FATAL(__VA_ARGS__) \
    }

#define VKE_VK_CHECK(op, ...) VKE_FATAL_IF(op != VK_SUCCESS, __VA_ARGS__)

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <cstdint>

namespace vke_common
{
    using AssetHandle = uint64_t;
    template <typename T>
    using SceneResult = std::expected<T, std::string>;

}

#endif
