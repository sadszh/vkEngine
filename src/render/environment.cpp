#include <render/environment.hpp>
#include <logger.hpp>
#include <string>
#include <algorithm>

namespace vke_render
{

    VkDevice globalLogicalDevice = nullptr;

    RenderEnvironment *RenderEnvironment::instance = nullptr;
    // using QueueFamilyIndices = RenderEnvironment::QueueFamilyIndices;
    // using SwapChainSupportDetails = RenderEnvironment::SwapChainSupportDetails;

    const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};

    static bool checkValidationLayerSupport()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char *layerName : validationLayers)
        {
            bool layerFound = false;

            for (const auto &layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return true;
    }

    void RenderEnvironment::createInstance()
    {
        VKE_FATAL_IF(enableValidationLayers && !checkValidationLayerSupport(), "Validation layers requested, but not available!")

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "test";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "NoEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        VKE_LOG_INFO("{} glfw extensions supported", glfwExtensionCount)

        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;

        const VkBool32 verboseValue = VK_TRUE;
        const VkLayerSettingEXT layerSetting = {"VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &verboseValue};
        VkLayerSettingsCreateInfoEXT layerSettingsCreateInfo = {VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr, 1, &layerSetting};

        if (enableValidationLayers)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
            createInfo.pNext = &layerSettingsCreateInfo;
        }
        else
        {
            createInfo.enabledLayerCount = 0;
        }

        VKE_VK_CHECK(vkCreateInstance(&createInfo, nullptr, &vkinstance), "Failed to create Vulkan instance!")
    }

    void RenderEnvironment::createSurface()
    {
        VKE_VK_CHECK(glfwCreateWindowSurface(vkinstance, window, nullptr, &surface), "Failed to create window surface!")
    }

    static bool checkQueueFamily(VkPhysicalDevice pdevice)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pdevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProperties;
        queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pdevice, &queueFamilyCount, queueFamilyProperties.data());

        VKE_LOG_INFO("Queue Family Cnt {}", queueFamilyCount);

        bool hasGraphicsQueue = false, hasComputeQueue = false, hasTransferQueue = false, hasPresentQueue = false;
        for (const auto &queueFamily : queueFamilyProperties)
        {
            VKE_LOG_INFO("Queue Family Flags {} Cnt {}", queueFamily.queueFlags, queueFamily.queueCount);
            hasGraphicsQueue |= queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
            hasComputeQueue |= queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT;
            hasTransferQueue |= queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT;
            hasPresentQueue |= queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
        }
        if (hasGraphicsQueue && hasComputeQueue && hasTransferQueue && hasPresentQueue)
            return true;
        return false;
    }

    void RenderEnvironment::setQueueFamilies(VkPhysicalDevice pdevice)
    {
        std::vector<VkQueueFamilyProperties> queueFamilyProperties;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pdevice, &queueFamilyCount, nullptr);
        queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pdevice, &queueFamilyCount, queueFamilyProperties.data());

        int i = 0;
        for (const auto &queueFamily : queueFamilyProperties)
        {
            if (!queueFamilyIndices.graphicsAndComputeFamily.has_value() &&
                (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
                queueFamilyIndices.graphicsAndComputeFamily = i;
            else if (!queueFamilyIndices.computeOnlyFamily.has_value() &&
                     (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT))
                queueFamilyIndices.computeOnlyFamily = i;
            else if (!queueFamilyIndices.transferOnlyFamily.has_value() &&
                     (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT))
                queueFamilyIndices.transferOnlyFamily = i;

            if (!queueFamilyIndices.presentFamily.has_value() && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(pdevice, i, instance->surface, &presentSupport);
                if (presentSupport)
                    queueFamilyIndices.presentFamily = i;
            }
            i++;
        }
        queueFamilyIndices.getUniqueQueueFamilies();
    }

    const std::vector<const char *> deviceExtensions =
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
         VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME,
         VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
         VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};

    static bool checkDeviceExtensionSupport(VkPhysicalDevice pdevice)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(pdevice, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(pdevice, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        for (const auto &extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    bool RenderEnvironment::isDeviceSuitable(VkPhysicalDevice pdevice)
    {
        bool extensionsSupported = checkDeviceExtensionSupport(pdevice);

        bool swapChainAdequate = false;
        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(pdevice);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        VkPhysicalDeviceFeatures2 supportedFeatures2 = {};
        supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceVulkan13Features supportedFeatures13 = {};
        supportedFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        supportedFeatures2.pNext = &supportedFeatures13;
        VkPhysicalDeviceVulkan12Features supportedFeatures12 = {};
        supportedFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        supportedFeatures13.pNext = &supportedFeatures12;
        VkPhysicalDeviceVulkan11Features supportedFeatures11 = {};
        supportedFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        supportedFeatures12.pNext = &supportedFeatures11;
        vkGetPhysicalDeviceFeatures2(pdevice, &supportedFeatures2);
        VkPhysicalDeviceFeatures &supportedFeatures = supportedFeatures2.features;

        return checkQueueFamily(pdevice) &&
               extensionsSupported &&
               swapChainAdequate &&
               supportedFeatures.samplerAnisotropy &&
               supportedFeatures.shaderInt64 &&
               supportedFeatures.shaderInt16 &&
               supportedFeatures.multiDrawIndirect &&
               supportedFeatures.fillModeNonSolid &&
               supportedFeatures11.storageBuffer16BitAccess &&
               supportedFeatures11.uniformAndStorageBuffer16BitAccess &&
               supportedFeatures12.shaderBufferInt64Atomics &&
               // supportedFeatures12.shaderSharedInt64Atomics &&
               supportedFeatures12.shaderFloat16 &&
               supportedFeatures12.shaderInt8 &&
               supportedFeatures12.uniformAndStorageBuffer8BitAccess &&
               supportedFeatures12.descriptorIndexing &&
               //    supportedFeatures12.shaderInputAttachmentArrayDynamicIndexing &&
               //    supportedFeatures12.shaderUniformTexelBufferArrayDynamicIndexing &&
               //    supportedFeatures12.shaderStorageTexelBufferArrayDynamicIndexing &&
               supportedFeatures12.shaderUniformBufferArrayNonUniformIndexing &&
               supportedFeatures12.shaderSampledImageArrayNonUniformIndexing &&
               supportedFeatures12.shaderStorageBufferArrayNonUniformIndexing &&
               //    supportedFeatures12.shaderStorageImageArrayNonUniformIndexing &&
               // supportedFeatures12.shaderInputAttachmentArrayNonUniformIndexing &&
               // supportedFeatures12.shaderUniformTexelBufferArrayNonUniformIndexing &&
               // supportedFeatures12.shaderStorageTexelBufferArrayNonUniformIndexing &&
               supportedFeatures12.descriptorBindingUniformBufferUpdateAfterBind &&
               supportedFeatures12.descriptorBindingSampledImageUpdateAfterBind &&
               // supportedFeatures12.descriptorBindingStorageImageUpdateAfterBind &&
               supportedFeatures12.descriptorBindingStorageBufferUpdateAfterBind &&
               // supportedFeatures12.descriptorBindingUniformTexelBufferUpdateAfterBind &&
               // supportedFeatures12.descriptorBindingStorageTexelBufferUpdateAfterBind &&
               supportedFeatures12.descriptorBindingUpdateUnusedWhilePending &&
               supportedFeatures12.descriptorBindingPartiallyBound &&
               supportedFeatures12.descriptorBindingVariableDescriptorCount &&
               supportedFeatures12.runtimeDescriptorArray &&
               supportedFeatures12.timelineSemaphore &&
               supportedFeatures13.shaderDemoteToHelperInvocation &&
               supportedFeatures13.synchronization2 &&
               supportedFeatures13.dynamicRendering;
    }

    void RenderEnvironment::pickPhysicalDevice()
    {
        physicalDevice = VK_NULL_HANDLE;
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vkinstance, &deviceCount, nullptr);
        VKE_LOG_INFO("{} devcies", deviceCount);

        std::vector<VkPhysicalDevice> devices(deviceCount);
        std::vector<VkPhysicalDevice> candidates;
        vkEnumeratePhysicalDevices(vkinstance, &deviceCount, devices.data());

        for (const auto &device : devices)
            if (isDeviceSuitable(device))
                candidates.push_back(device);

        uint64_t maxTotMemory = 0, maxLocalMemory = 0;
        VkPhysicalDevice bestDevice;

        for (auto device : candidates)
        {
            VkPhysicalDeviceMemoryProperties memoryProperties;
            vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);

            uint64_t totMemory = 0, localMemory = 0;
            for (int i = 0; i < memoryProperties.memoryHeapCount; i++)
            {
                VkMemoryHeap &heap = memoryProperties.memoryHeaps[i];
                VKE_LOG_INFO("SIZE {} FLAGS {}", heap.size * 1.0f / (1024.0 * 1024.0 * 1024.0), heap.flags);
                totMemory += heap.size;
                if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                    localMemory += heap.size;
            }
            VKE_LOG_INFO("TOT MEMORY SIZE {} LOCAL MEMORY  {}", totMemory * 1.0f / (1024.0 * 1024.0 * 1024.0), localMemory);

            if (localMemory > maxLocalMemory)
            {
                maxLocalMemory = localMemory;
                maxTotMemory = totMemory;
                bestDevice = device;
            }
            else if (localMemory == maxLocalMemory && totMemory > maxTotMemory)
            {
                maxLocalMemory = localMemory;
                maxTotMemory = totMemory;
                bestDevice = device;
            }
        }

        if (candidates.size())
        {
            VKE_LOG_INFO("FIND {} DEVICES", candidates.size());
            physicalDevice = bestDevice;
            setQueueFamilies(physicalDevice);
            vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
            VKE_LOG_INFO(std::string(physicalDeviceProperties.deviceName));
            // exit(0);
        }
        else
            VKE_FATAL("Failed to find a suitable GPU!")
    }

    void RenderEnvironment::createLogicalDevice()
    {
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

        float queuePriority = 1.0f;
        for (uint32_t queueFamilyIndex : queueFamilyIndices.uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceFeatures &deviceFeatures = deviceFeatures2.features;
        deviceFeatures.fillModeNonSolid = VK_TRUE;
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.shaderInt64 = VK_TRUE;
        deviceFeatures.shaderInt16 = VK_TRUE;
        deviceFeatures.multiDrawIndirect = VK_TRUE;

        VkPhysicalDeviceVulkan13Features deviceFeatures13 = {};
        deviceFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        deviceFeatures13.shaderDemoteToHelperInvocation = VK_TRUE;
        deviceFeatures13.synchronization2 = VK_TRUE;
        deviceFeatures13.dynamicRendering = VK_TRUE;
        deviceFeatures2.pNext = &deviceFeatures13;

        VkPhysicalDeviceVulkan12Features deviceFeatures12 = {};
        deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        deviceFeatures12.shaderBufferInt64Atomics = VK_TRUE;
        // deviceFeatures12.shaderSharedInt64Atomics = VK_TRUE;
        deviceFeatures12.shaderFloat16 = VK_TRUE;
        deviceFeatures12.shaderInt8 = VK_TRUE;
        deviceFeatures12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
        deviceFeatures12.descriptorIndexing = VK_TRUE;
        // deviceFeatures12.shaderInputAttachmentArrayDynamicIndexing = VK_TRUE;
        // deviceFeatures12.shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE;
        // deviceFeatures12.shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE;
        deviceFeatures12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
        deviceFeatures12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        deviceFeatures12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        // deviceFeatures12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
        // deviceFeatures12.shaderInputAttachmentArrayNonUniformIndexing = VK_TRUE;
        // deviceFeatures12.shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
        // deviceFeatures12.shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
        deviceFeatures12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
        deviceFeatures12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        // deviceFeatures12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        deviceFeatures12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        // deviceFeatures12.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
        // deviceFeatures12.descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
        deviceFeatures12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        deviceFeatures12.descriptorBindingPartiallyBound = VK_TRUE;
        deviceFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
        deviceFeatures12.runtimeDescriptorArray = VK_TRUE;
        deviceFeatures12.timelineSemaphore = VK_TRUE;
        deviceFeatures13.pNext = &deviceFeatures12;

        VkPhysicalDeviceVulkan11Features deviceFeatures11 = {};
        deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        deviceFeatures11.pNext = nullptr;
        deviceFeatures11.storageBuffer16BitAccess = VK_TRUE;
        deviceFeatures11.uniformAndStorageBuffer16BitAccess = VK_TRUE;
        deviceFeatures12.pNext = &deviceFeatures11;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = queueCreateInfos.size();
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        // createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.pEnabledFeatures = nullptr;
        createInfo.pNext = &deviceFeatures2;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        createInfo.enabledLayerCount = 0;

        VKE_VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, nullptr, &globalLogicalDevice), "Failed to create logical device!")

        VkQueue queue;
        vkGetDeviceQueue(globalLogicalDevice, queueFamilyIndices.graphicsAndComputeFamily.value(), 0, &queue);
        commandQueues[GRAPHICS_QUEUE] = std::make_unique<GPUCommandQueue>(queue);

        if (queueFamilyIndices.computeOnlyFamily.has_value())
        {
            vkGetDeviceQueue(globalLogicalDevice, queueFamilyIndices.computeOnlyFamily.value(), 0, &queue);
            commandQueues[COMPUTE_QUEUE] = std::make_unique<GPUCommandQueue>(queue);
        }
        else
            commandQueues[COMPUTE_QUEUE] = nullptr;

        if (queueFamilyIndices.transferOnlyFamily.has_value())
        {
            vkGetDeviceQueue(globalLogicalDevice, queueFamilyIndices.transferOnlyFamily.value(), 0, &queue);
            commandQueues[TRANSFER_QUEUE] = std::make_unique<GPUCommandQueue>(queue);
        }
        else
            commandQueues[TRANSFER_QUEUE] = nullptr;

        vkGetDeviceQueue(globalLogicalDevice, queueFamilyIndices.presentFamily.value(), 0, &presentQueue);
    }

    void RenderEnvironment::createVulkanMemoryAllocator()
    {
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_4;
        allocatorCreateInfo.physicalDevice = physicalDevice;
        allocatorCreateInfo.device = globalLogicalDevice;
        allocatorCreateInfo.instance = vkinstance;

        VKE_VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator), "failed to create VMA allocator!")
    }

    void RenderEnvironment::createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsAndComputeFamily.value();
        vkCreateCommandPool(globalLogicalDevice, &poolInfo, nullptr, &commandPool);

        if (queueFamilyIndices.computeOnlyFamily.has_value())
        {
            poolInfo.queueFamilyIndex = queueFamilyIndices.computeOnlyFamily.value();
            vkCreateCommandPool(globalLogicalDevice, &poolInfo, nullptr, &computeCommandPool);
        }
        else
        {
            computeCommandPool = commandPool;
        }

        if (queueFamilyIndices.transferOnlyFamily.has_value())
        {
            poolInfo.queueFamilyIndex = queueFamilyIndices.transferOnlyFamily.value();
            vkCreateCommandPool(globalLogicalDevice, &poolInfo, nullptr, &transferCommandPool);
        }
        else
        {
            transferCommandPool = commandPool;
        }
    }

    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
    {
        for (const auto &availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
    {
        for (const auto &availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(RenderEnvironment::GetInstance()->window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)};

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    static VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(RenderEnvironment::GetInstance()->physicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        VKE_FATAL("Failed to find supported format!")
    }

    static VkFormat findDepthFormat()
    {
        return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    void RenderEnvironment::createSwapChain()
    {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice);
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        imageCnt = swapChainSupport.capabilities.minImageCount + 1;

        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCnt > swapChainSupport.capabilities.maxImageCount)
        {
            imageCnt = swapChainSupport.capabilities.maxImageCount;
        }
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;

        createInfo.minImageCount = imageCnt;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t qfIndices[] = {queueFamilyIndices.graphicsAndComputeFamily.value(), queueFamilyIndices.presentFamily.value()};
        if (queueFamilyIndices.graphicsAndComputeFamily != queueFamilyIndices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = qfIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;     // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VKE_VK_CHECK(vkCreateSwapchainKHR(globalLogicalDevice, &createInfo, nullptr, &swapChain), "Failed to create swap chain!")

        vkGetSwapchainImagesKHR(globalLogicalDevice, swapChain, &imageCnt, nullptr);
        swapChainImages.resize(imageCnt);
        vkGetSwapchainImagesKHR(globalLogicalDevice, swapChain, &imageCnt, swapChainImages.data());

        VKE_LOG_INFO("IMAGE COUNT {}", imageCnt);

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;

        depthFormat = findDepthFormat();

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            CreateImage(extent.width, extent.height, depthFormat,
                        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &depthImages[i], &depthImageVmaAllocations[i], nullptr);

        VkCommandBuffer tmpCmdBuffer = BeginSingleTimeCommands(commandPool);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            MakeLayoutTransition(tmpCmdBuffer, VK_ACCESS_NONE, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, depthImages[i], VK_IMAGE_ASPECT_DEPTH_BIT, 1,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
        EndSingleTimeCommands(GetGraphicsQueue(), commandPool, tmpCmdBuffer);
    }

    void RenderEnvironment::createSyncObjects()
    {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(imageCnt);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VKE_FATAL_IF(vkCreateSemaphore(globalLogicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                             vkCreateFence(globalLogicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS,
                         "Failed to create synchronization objects for a frame!")
        }

        for (int i = 0; i < imageCnt; i++)
        {
            VKE_FATAL_IF(vkCreateSemaphore(globalLogicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS,
                         "Failed to create synchronization objects for a frame!")
        }

        VkSemaphoreTypeCreateInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;
        semaphoreInfo.pNext = &timelineInfo;
    }

    void RenderEnvironment::cleanupSwapChain()
    {
        for (auto imageView : instance->swapChainImageViews)
            vkDestroyImageView(globalLogicalDevice, imageView, nullptr);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroyImageView(globalLogicalDevice, instance->depthImageViews[i], nullptr);
            vmaDestroyImage(instance->vmaAllocator, instance->depthImages[i], instance->depthImageVmaAllocations[i]);
        }

        vkDestroySwapchainKHR(globalLogicalDevice, swapChain, nullptr);
    }

    void RenderEnvironment::recreateSwapChain()
    {
        vkDeviceWaitIdle(globalLogicalDevice);
        ((CPUCommandQueue *)commandQueues[CPU_QUEUE].get())->WaitIdle();
        cleanupSwapChain();
        createSwapChain();
        createImageViews();
        rootRenderContext.width = swapChainExtent.width;
        rootRenderContext.height = swapChainExtent.height;
        rootRenderContext.colorImages = swapChainImages;
        rootRenderContext.colorImageViews = swapChainImageViews;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            rootRenderContext.depthImages[i] = depthImages[i];
            rootRenderContext.depthImageViews[i] = depthImageViews[i];
        }
        resizeEventHub.DispatchEvent(&rootRenderContext);
        vkDeviceWaitIdle(globalLogicalDevice);
    }

    void RenderEnvironment::createImageViews()
    {
        swapChainImageViews.resize(imageCnt);

        for (size_t i = 0; i < imageCnt; i++)
        {
            swapChainImageViews[i] = RenderEnvironment::CreateImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            depthImageViews[i] = RenderEnvironment::CreateImageView(depthImages[i], depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        }
    }

    void RenderEnvironment::createCPUCommandQueue()
    {
        std::unique_ptr<CPUCommandQueue> cpuQueue = std::make_unique<CPUCommandQueue>(globalLogicalDevice);
        cpuQueue->Start();
        commandQueues[CPU_QUEUE] = std::move(cpuQueue);
    }
};
