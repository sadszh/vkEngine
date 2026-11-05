#include <common.hpp>
#include <script.hpp>
#include <logger.hpp>
#include <iostream>
#include <fstream>
#include <game_config.hpp>
#include <reflect/value_view.hpp>

#ifdef _WIN32
#include <windows.h>
#define LOAD_LIBRARY(path) LoadLibraryW(path)
#define GET_PROC_ADDRESS(lib, name) GetProcAddress((HMODULE)lib, name)
#define LIB_HANDLE HMODULE
#else
#include <dlfcn.h>
#define LOAD_LIBRARY(path) dlopen(path, RTLD_LAZY | RTLD_LOCAL)
#define GET_PROC_ADDRESS(lib, name) dlsym(lib, name)
#define LIB_HANDLE void *
#endif

namespace vke_common
{
    SceneResult<std::vector<std::string>> ScriptManager::GetScriptList(entt::entity entity)
    {
        std::vector<std::string> scripts;
        const auto receive = +[](void *opaque, const char *className)
        {
            static_cast<std::vector<std::string> *>(opaque)->emplace_back(className);
        };
        if (!instance->csharpExports.sceneManagerFunctions.getScriptList(entity, &scripts, receive))
            return std::unexpected("managed script listing failed");
        return scripts;
    }

    SceneResult<TypeInfoData> ScriptManager::GetScriptData(entt::entity entity, const std::string &className)
    {
        SceneResult<TypeInfoData> result = std::unexpected("managed script export returned no data");
        const auto receive = +[](void *opaque, const std::byte *bytes, int32_t size)
        {
            auto &result = *static_cast<SceneResult<TypeInfoData> *>(opaque);
            if (size < 0 || (size != 0 && !bytes))
            {
                result = std::unexpected("invalid script export buffer");
                return;
            }
            TypeInfoData data;
            if (size != 0) data.assign(bytes, bytes + size);
            result = std::move(data);
        };
        if (!instance->csharpExports.sceneManagerFunctions.getScriptData(entity, className.c_str(), &result, receive))
            return std::unexpected("managed script export failed");
        return result;
    }

    SceneResult<void> ScriptManager::SetScriptData(entt::entity entity, const std::string &className, const TypeInfoData &data)
    {
        if (!instance->csharpExports.sceneManagerFunctions.setScriptData(
                entity, className.c_str(), data.data(), static_cast<int32_t>(data.size())))
            return std::unexpected("managed script edit failed");
        return {};
    }

    SceneResult<std::vector<vke_component::ScriptStateData>> ScriptManager::GetEntityScriptsData(entt::entity entity)
    {
        struct ExportContext
        {
            std::vector<vke_component::ScriptStateData> scripts;
            std::string error;
        } context;
        const auto receive = +[](void *opaque, const char *className, const std::byte *bytes, int32_t size)
        {
            auto &result = *static_cast<ExportContext *>(opaque);
            if (!result.error.empty()) return;
            auto type = className ? instance->FindTypeInfo(className) : nullptr;
            if (!type || size < 0 || (size != 0 && !bytes))
            {
                result.error = "invalid script export schema or size";
                return;
            }
            auto value = ValueView::Parse(type, std::span<const std::byte>(bytes, static_cast<size_t>(size)));
            if (!value)
            {
                result.error = std::string("invalid script export: ") + std::string(ToString(value.error().code));
                return;
            }
            vke_component::ScriptStateData state;
            state.className = className;
            if (size != 0) state.data.assign(bytes, bytes + size);
            result.scripts.push_back(std::move(state));
        };
        if (!instance->csharpExports.sceneManagerFunctions.getEntityScriptsData(entity, &context, receive))
            return std::unexpected("managed script export failed");
        if (!context.error.empty()) return std::unexpected(context.error);
        return std::move(context.scripts);
    }

    static const char_t *CSHARP_TYPE_NAME = L"vkEngine.EngineCore.EntryPoint, EngineCore";
    static const char_t *CSHARP_GET_EXPORTS_METHOD_NAME = L"GetCSharpExports";

    typedef void(CORECLR_DELEGATE_CALLTYPE *CSharpSideGetExportsFunction)(CSharpExports *);

    static hostfxr_initialize_for_runtime_config_fn initFptr = nullptr;
    static hostfxr_get_runtime_delegate_fn getDelegateFptr = nullptr;
    static hostfxr_close_fn closeFptr = nullptr;

    static bool loadHostFXR()
    {
        char_t buffer[MAX_PATH];
        size_t buffer_size = sizeof(buffer) / sizeof(char_t);
        int rc = get_hostfxr_path(buffer, &buffer_size, nullptr);
        if (rc != 0)
            return false;

        void *lib = LOAD_LIBRARY(buffer);
        initFptr = (hostfxr_initialize_for_runtime_config_fn)GET_PROC_ADDRESS(lib, "hostfxr_initialize_for_runtime_config");
        getDelegateFptr = (hostfxr_get_runtime_delegate_fn)GET_PROC_ADDRESS(lib, "hostfxr_get_runtime_delegate");
        closeFptr = (hostfxr_close_fn)GET_PROC_ADDRESS(lib, "hostfxr_close");

        return (initFptr && getDelegateFptr && closeFptr);
    }

    static void getDotnetLoadAssembly(const char_t *config_path, DelegateFunctionPointers &functionPointers)
    {
        hostfxr_handle cxt = nullptr;
        int rc = initFptr(config_path, nullptr, &cxt);
        VKE_FATAL_IF(rc != 0 || cxt == nullptr, "HostFxr Init failed: {}", rc)

        rc = getDelegateFptr(
            cxt,
            hdt_load_assembly_and_get_function_pointer,
            (void **)&(functionPointers.loadAssemblyAndGetFunctionPointer));
        VKE_FATAL_IF(rc != 0 || functionPointers.loadAssemblyAndGetFunctionPointer == nullptr, "Get loadAssemblyAndGetFunctionPointer failed: {}", rc)

        rc = getDelegateFptr(
            cxt,
            hdt_load_assembly,
            (void **)&(functionPointers.loadAssembly));
        VKE_FATAL_IF(rc != 0 || functionPointers.loadAssembly == nullptr, "Get loadAssembly failed: {}", rc)

        rc = getDelegateFptr(
            cxt,
            hdt_get_function_pointer,
            (void **)&(functionPointers.getFunctionPointer));
        VKE_FATAL_IF(rc != 0 || functionPointers.getFunctionPointer == nullptr, "Get getFunctionPointer failed: {}", rc)

        closeFptr(cxt);
    }

    ScriptManager *ScriptManager::instance = nullptr;

    void ScriptManager::getFunctionPointer(const char_t *typeName, const char_t *methodName, void **func)
    {
        int rc = functionPointers.getFunctionPointer(
            typeName, methodName, UNMANAGEDCALLERSONLY_METHOD, nullptr, nullptr, func);
        VKE_FATAL_IF(rc != 0 || *func == nullptr, "getFunctionPointer failed: {}", rc)
    }

    void ScriptManager::init()
    {
        VKE_FATAL_IF(!loadHostFXR(), "Failed to load hostfxr")
        const auto runtimeConfig = Paths::Engine("csharp/EngineCore.runtimeconfig.json");
        const auto coreAssembly = Paths::Engine("csharp/EngineCore.dll");
        getDotnetLoadAssembly(runtimeConfig.c_str(), functionPointers);
        functionPointers.loadAssembly(coreAssembly.c_str(), nullptr, nullptr);

        const std::string &gameAssemblyPath = GameConfig::GetInstance()->gameScriptPath;
        if (gameAssemblyPath.length() > 0)
        {
            functionPointers.loadAssembly(Paths::Project(PathFromUTF8(gameAssemblyPath)).c_str(), nullptr, nullptr);
            const std::string &typeInfoPath =
                GameConfig::GetInstance()->gameScriptTypeInfoPath;
            VKE_FATAL_IF(typeInfoPath.empty(),
                         "gameScriptTypeInfoPath is required when gameScriptPath is configured")
            loadTypeInfos(Paths::Project(PathFromUTF8(typeInfoPath)));
        }

        getCSharpExports();
        int rc = csharpExports.init();
        VKE_FATAL_IF(rc != 0, "C# Init failed: {}", rc)
        registerNativeFunctions();
    }

    void ScriptManager::loadTypeInfos(const std::filesystem::path &path)
    {
        std::ifstream input(path);
        VKE_FATAL_IF(!input, "Failed to open game script type information: {}", PathToUTF8(path))

        const nlohmann::json document = nlohmann::json::parse(input, nullptr, false);
        VKE_FATAL_IF(document.is_discarded() || !document.is_object(),
                     "Invalid game script type information JSON: {}", PathToUTF8(path))

        const auto types = document.find("types");
        VKE_FATAL_IF(types == document.end(),
                     "Game script type information is missing field $.types: {}", PathToUTF8(path))
        VKE_FATAL_IF(!types->is_array(),
                     "Game script type information field $.types must be an array: {}", PathToUTF8(path))

        typeInfos.clear();
        typeInfos.reserve(types->size());
        for (std::size_t i = 0; i < types->size(); ++i)
        {
            auto type = TypeInfo::FromJson((*types)[i]);
            if (!type)
            {
                VKE_FATAL("Invalid game script TypeInfo at $.types[{}]{}: {} ({})",
                          i, type.error().path.substr(1),
                          ToString(type.error().code), PathToUTF8(path))
            }

            const std::string name = (*type)->Name();
            const auto [_, inserted] = typeInfos.emplace(name, std::move(*type));
            VKE_FATAL_IF(!inserted,
                         "Duplicate game script TypeInfo name '{}' at $.types[{}]: {}",
                         name, i, PathToUTF8(path))
        }
    }

    void ScriptManager::getCSharpExports()
    {
        CSharpSideGetExportsFunction getExportsFunc = nullptr;
        getFunctionPointer(CSHARP_TYPE_NAME,
                           CSHARP_GET_EXPORTS_METHOD_NAME,
                           (void **)&getExportsFunc);
        getExportsFunc(&csharpExports);
    }
}
