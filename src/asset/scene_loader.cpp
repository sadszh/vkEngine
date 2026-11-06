#include <scene.hpp>
#include <algorithm>

namespace vke_common
{
    Result<SceneData> AssetManager::LoadSceneFile(const std::filesystem::path &path)
    {
        auto json = LoadJSON(path);
        if (!json) return std::unexpected(json.error());
        auto result = SceneData::FromJSON(*json);
        if (!result) return std::unexpected(PathToUTF8(path) + ": " + result.error());
        return result;
    }

    Result<std::shared_ptr<const SceneData>> AssetManager::LoadSceneData(AssetHandle handle)
    {
        if (!instance) return std::unexpected("AssetManager is not initialized");
        std::vector<AssetHandle> stack;
        return instance->loadSceneData(handle, stack);
    }

    Result<std::shared_ptr<const SceneData>> AssetManager::loadSceneData(
        AssetHandle handle, std::vector<AssetHandle> &stack)
    {
        const std::string context = "prefab " + std::to_string(handle) + ": ";
        if (std::find(stack.begin(), stack.end(), handle) != stack.end())
            return std::unexpected(context + "cyclic prefab reference");
        if (stack.size() >= 128) return std::unexpected(context + "prefab nesting limit exceeded");
        auto *asset = GetSceneAsset(handle);
        if (!asset || handle == 0) return std::unexpected(context + "asset not found");
        if (asset->val) return asset->val;
        auto parsed = LoadSceneFile(resolveAssetPath(handle, asset->path));
        if (!parsed) return std::unexpected(context + parsed.error());
        if (auto valid = parsed->ValidatePrefab(); !valid) return std::unexpected(context + valid.error());
        std::unordered_set<AssetHandle> dependencies;
        for (auto entity : parsed->registry.view<PrefabReference>())
            dependencies.insert(parsed->registry.get<PrefabReference>(entity).scene.Handle());
        stack.push_back(handle);
        auto expanded = expandSceneData(*parsed, stack);
        stack.pop_back();
        if (!expanded) return std::unexpected(context + expanded.error());
        // Publish only complete snapshots and their dependency edges.
        for (auto dependency : sceneDependencies[handle])
            sceneDependents[dependency].erase(handle);
        sceneDependencies[handle] = std::move(dependencies);
        for (auto dependency : sceneDependencies[handle])
            sceneDependents[dependency].insert(handle);
        asset->val = std::make_shared<SceneData>(std::move(*parsed));
        return asset->val;
    }

    void AssetManager::InvalidateSceneData(AssetHandle handle)
    {
        if (!instance) return;
        std::unordered_set<AssetHandle> affected;
        std::vector<AssetHandle> pending{handle};
        while (!pending.empty())
        {
            auto current = pending.back();
            pending.pop_back();
            if (!affected.insert(current).second) continue;
            if (auto it = instance->sceneDependents.find(current); it != instance->sceneDependents.end())
                pending.insert(pending.end(), it->second.begin(), it->second.end());
        }
        for (auto current : affected)
        {
            if (auto *asset = GetSceneAsset(current)) asset->val.reset();
            if (auto it = instance->sceneDependencies.find(current); it != instance->sceneDependencies.end())
            {
                for (auto dependency : it->second) instance->sceneDependents[dependency].erase(current);
                instance->sceneDependencies.erase(it);
            }
        }
        for (auto current : affected) instance->sceneDependents.erase(current);
    }

    Result<std::shared_ptr<const SceneData>> AssetManager::ReloadSceneData(AssetHandle handle)
    {
        InvalidateSceneData(handle);
        return LoadSceneData(handle);
    }

    Result<void> AssetManager::expandSceneData(SceneData &data, std::vector<AssetHandle> &stack)
    {
        if (data.stage == SceneDataStage::Expanded || data.stage == SceneDataStage::Ready) return {};
        // Parsed data has already passed ingestion checks.
        // Build separately so errors leave the caller's data and stage unchanged.
        SceneData result = data.Clone();
        std::vector<std::pair<vke_ds::id32_t, entt::entity>> references;
        for (const auto &[id, entity] : result.idToEntity)
            if (result.registry.all_of<PrefabReference>(entity)) references.emplace_back(id, entity);
        std::sort(references.begin(), references.end());
        for (const auto &[id, targetRoot] : references)
        {
            auto reference = result.registry.get<PrefabReference>(targetRoot);
            auto loaded = loadSceneData(reference.scene.Handle(), stack);
            if (!loaded) return std::unexpected("entity " + std::to_string(id) + ": " + loaded.error());
            reference.scene.Resolve(*loaded);
            const SceneData &source = *reference.scene.Get();
            std::unordered_map<entt::entity, entt::entity> mapping{{source.prefabRoot, targetRoot}};
            std::vector<vke_ds::id32_t> ids;
            for (const auto &[sourceID, entity] : source.idToEntity) ids.push_back(sourceID);
            std::sort(ids.begin(), ids.end());
            for (auto sourceID : ids)
            {
                const auto entity = source.idToEntity.at(sourceID);
                if (entity == source.prefabRoot) continue;
                if (result.maxID == UINT32_MAX) return std::unexpected("expanded entity ID space exhausted");
                auto destination = result.registry.create();
                mapping[entity] = destination;
                result.idToEntity[result.maxID++] = destination;
                const auto &object = source.registry.get<GameObject>(entity);
                auto name = object.name;
                result.registry.emplace<GameObject>(destination, name, object.isStatic);
                source.CopyComponents(entity, result, destination);
            }
            for (const auto &[entity, destination] : mapping)
                if (entity != source.prefabRoot) result.parents[destination] = mapping.at(source.parents.at(entity));

            const auto &defaults = source.registry.get<GameObject>(source.prefabRoot);
            auto &object = result.registry.get<GameObject>(targetRoot);
            if (!reference.overrideName) object.name = defaults.name;
            if (!reference.overrideStatic) object.isStatic = defaults.isStatic;
            const auto originalEntity = data.idToEntity.at(id);
            source.CopyComponents(source.prefabRoot, result, targetRoot);
            const auto inheritedTransform = result.registry.get<TransformData>(targetRoot);
            SceneData::ScriptDataList scripts;
            if (auto *inherited = result.registry.try_get<SceneData::ScriptDataList>(targetRoot)) scripts = *inherited;
            data.CopyComponents(originalEntity, result, targetRoot);
            if (!reference.overrideTransform) result.registry.replace<TransformData>(targetRoot, inheritedTransform);
            if (const auto *overrides = data.registry.try_get<SceneData::ScriptDataList>(originalEntity))
                for (const auto &script : *overrides)
                {
                    auto found = std::find_if(scripts.begin(), scripts.end(), [&](const auto &s) { return s.className == script.className; });
                    if (found == scripts.end()) scripts.push_back(script);
                    else *found = script;
                }
            if (!scripts.empty()) result.registry.emplace_or_replace<SceneData::ScriptDataList>(targetRoot, std::move(scripts));
            result.registry.remove<PrefabReference>(targetRoot);
        }
        // Remapping a validated prefab onto one existing node preserves the
        // hierarchy; only the expansion postcondition needs checking here.
        if (auto valid = result.ValidateExpanded(); !valid) return valid;
        result.stage = SceneDataStage::Expanded;
        data = std::move(result);
        return {};
    }

    Result<void> AssetManager::resolveSceneAssets(SceneData &data)
    {
        if (data.stage == SceneDataStage::Ready) return {};
        if (data.stage != SceneDataStage::Expanded) return std::unexpected("expand SceneData before resolving assets");
        auto visitComponents = [&](auto operation) -> Result<void>
        {
            for (const auto &[id, entity] : data.idToEntity)
            {
                auto visitComponent = [&]<typename T>() -> Result<void>
                {
                    if (auto *component = data.registry.try_get<T>(entity))
                        return operation(*component);
                    return {};
                };
                using namespace vke_component;
                Result<void> result;
                if (!(result = visitComponent.operator()<RenderableObjectData>()) ||
                    !(result = visitComponent.operator()<SkeletonAnimatorData>()) ||
                    !(result = visitComponent.operator()<CanvasData>()) ||
                    !(result = visitComponent.operator()<AudioSourceData>()))
                    return std::unexpected("entity " + std::to_string(id) + ": " + result.error());
            }
            return {};
        };
        // Validate every component dependency before any GPU/audio asset loading.
        if (auto result = visitComponents([](const auto &component) { return component.ValidateAssets(); }); !result)
            return result;
        if (auto result = visitComponents([](auto &component) { return component.LoadAssets(); }); !result)
            return result;
        data.stage = SceneDataStage::Ready;
        return {};
    }

    Result<void> AssetManager::PrepareSceneData(SceneData &data)
    {
        if (!instance) return std::unexpected("AssetManager is not initialized");
        std::vector<AssetHandle> stack;
        if (auto result = instance->expandSceneData(data, stack); !result) return result;
        return instance->resolveSceneAssets(data);
    }
}
