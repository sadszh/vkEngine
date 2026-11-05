#ifndef SCRIPT_H
#define SCRIPT_H

#include <dotnet/nethost.h>
#include <dotnet/coreclr_delegates.h>
#include <dotnet/hostfxr.h>

#include <interop/native.hpp>
#include <component.hpp>
#include <reflect/type_info.hpp>
#include <component/script.hpp>

#include <entt/entity/entity.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vke_common
{

    struct CSharpScriptLoadData
    {
        uint32_t entity;
        const char *className;
        const std::byte *data;
        int32_t dataSize;
    };

    struct DelegateFunctionPointers
    {
        load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointer;
        load_assembly_fn loadAssembly;
        get_function_pointer_fn getFunctionPointer;

        DelegateFunctionPointers()
            : loadAssemblyAndGetFunctionPointer(nullptr),
              loadAssembly(nullptr),
              getFunctionPointer(nullptr) {}
    };

    struct CSharpSceneManagerFunctions
    {
        void (*load)(const CSharpScriptLoadData *, uint32_t);
        void (*start)(const entt::entity *, uint32_t);
        void (*startAll)();
        void (*update)();
        void (*fixedUpdate)();
        void (*lateUpdate)();
        void (*unload)();
        void (*unloadEntities)(const entt::entity *, uint32_t);
        void (*reset)();
        void (*unregisterComponentCallbacks)(entt::entity, int32_t);
        int32_t (*hasScripts)(entt::entity);
        int32_t (*getScriptList)(entt::entity, void *, void (*)(void *, const char *));
        int32_t (*getScriptData)(entt::entity, const char *, void *, void (*)(void *, const std::byte *, int32_t));
        int32_t (*setScriptData)(entt::entity, const char *, const std::byte *, int32_t);
        int32_t (*getEntityScriptsData)(entt::entity, void *, void (*)(void *, const char *, const std::byte *, int32_t));

        CSharpSceneManagerFunctions()
            : load(nullptr),
              start(nullptr),
              startAll(nullptr),
              update(nullptr),
              fixedUpdate(nullptr),
              lateUpdate(nullptr),
              unload(nullptr),
              unloadEntities(nullptr),
              reset(nullptr),
              unregisterComponentCallbacks(nullptr),
              hasScripts(nullptr),
              getScriptList(nullptr),
              getScriptData(nullptr),
              setScriptData(nullptr),
              getEntityScriptsData(nullptr) {}
    };

    struct CSharpExports
    {
        int (*init)();
        void (*registerNativeFunctions)(vke_interop::NativeFunctions *);
        CSharpSceneManagerFunctions sceneManagerFunctions;
        vke_interop::CSharpPhysicsFunctions physicsFunctions;
        vke_interop::CSharpUIFunctions uiFunctions;

        CSharpExports()
            : init(nullptr),
              registerNativeFunctions(nullptr),
              sceneManagerFunctions() {}
    };

    class ScriptManager
    {
    private:
        static ScriptManager *instance;
        ScriptManager() {}
        ~ScriptManager() {}

    public:
        static ScriptManager *GetInstance()
        {
            return instance;
        }

        static ScriptManager *Init()
        {
            instance = new ScriptManager();
            instance->init();
            return instance;
        }

        static void Dispose()
        {
            if (instance == nullptr)
                return;
            delete instance;
            instance = nullptr;
        }

        static void Load(const CSharpScriptLoadData *data, uint32_t cnt)
        {
            instance->csharpExports.sceneManagerFunctions.load(data, cnt);
        }

        static void Start(const std::vector<entt::entity> &entities)
        {
            instance->csharpExports.sceneManagerFunctions.start(
                entities.data(), static_cast<uint32_t>(entities.size()));
        }

        static void StartAll()
        {
            instance->csharpExports.sceneManagerFunctions.startAll();
        }

        static void Update()
        {
            instance->csharpExports.sceneManagerFunctions.update();
        }

        static void FixedUpdate()
        {
            instance->csharpExports.sceneManagerFunctions.fixedUpdate();
        }

        static void LateUpdate()
        {
            instance->csharpExports.sceneManagerFunctions.lateUpdate();
        }

        static void Unload()
        {
            instance->csharpExports.sceneManagerFunctions.unload();
        }

        static void UnloadEntities(const std::vector<entt::entity> &entities)
        {
            instance->csharpExports.sceneManagerFunctions.unloadEntities(
                entities.data(), static_cast<uint32_t>(entities.size()));
        }

        static void Reset()
        {
            instance->csharpExports.sceneManagerFunctions.reset();
        }

        static void UnregisterComponentCallbacks(entt::entity entity, ComponentType componentType)
        {
            instance->csharpExports.sceneManagerFunctions.unregisterComponentCallbacks(
                entity, static_cast<int32_t>(componentType));
        }

        static bool HasScripts(entt::entity entity)
        {
            return instance->csharpExports.sceneManagerFunctions.hasScripts(entity) != 0;
        }

        static const vke_interop::CSharpPhysicsFunctions &GetPhysicsFunctions()
        {
            return instance->csharpExports.physicsFunctions;
        }

        static const vke_interop::CSharpUIFunctions &GetUIFunctions()
        {
            return instance->csharpExports.uiFunctions;
        }

        static SceneResult<std::vector<std::string>> GetScriptList(entt::entity entity);
        static SceneResult<TypeInfoData> GetScriptData(entt::entity entity, const std::string &className);
        static SceneResult<void> SetScriptData(entt::entity entity, const std::string &className, const TypeInfoData &data);
        static SceneResult<std::vector<vke_component::ScriptStateData>> GetEntityScriptsData(entt::entity entity);

        TypeInfoPtr FindTypeInfo(std::string_view name) const
        {
            const auto value = typeInfos.find(std::string(name));
            return value == typeInfos.end() ? nullptr : value->second;
        }

        const std::unordered_map<std::string, TypeInfoPtr> &TypeInfos() const noexcept
        {
            return typeInfos;
        }

    private:
        DelegateFunctionPointers functionPointers;
        CSharpExports csharpExports;
        std::unordered_map<std::string, TypeInfoPtr> typeInfos;

        void init();
        void loadTypeInfos(const std::filesystem::path &path);

        void registerNativeFunctions();
        void getCSharpExports();
        void getFunctionPointer(const char_t *typeName, const char_t *methodName, void **func);
    };
}

#endif
