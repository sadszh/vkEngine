#include <scene.hpp>
#include <algorithm>
#include <tuple>

namespace vke_common
{
    SceneData::SceneData(const nlohmann::json &json)
    {
        auto parsed = FromJSON(json);
        VKE_FATAL_IF(!parsed, "Invalid SceneData: {}", parsed.error())
        *this = std::move(*parsed);
    }

    Result<SceneData> SceneData::FromJSON(const nlohmann::json &json)
    {
        if (!json.is_object() || !json.contains("objects") || !json["objects"].is_array())
            return std::unexpected("scene requires an objects array");
        for (const auto *key : {"maxid", "prefabRoot"})
            if (json.contains(key) && !json_validation::Unsigned(json[key], UINT32_MAX - 1)) return std::unexpected(std::string("invalid ") + key);
        SceneData data;
        data.maxID = std::max(data.maxID, json.value("maxid", vke_ds::id32_t{1}));
        for (const auto &object : json["objects"])
        {
            if (!object.is_object() || !object.contains("id") || !json_validation::Unsigned(object["id"], UINT32_MAX - 1) || object["id"] == 0)
                return std::unexpected("entity requires a nonzero uint32 id");
            const auto id = object["id"].get<vke_ds::id32_t>();
            const std::string path = "entity " + std::to_string(id) + ": ";
            if (data.idToEntity.contains(id)) return std::unexpected(path + "duplicate id");
            if (object.contains("prefab") && !json_validation::Unsigned(object["prefab"])) return std::unexpected(path + "invalid prefab handle");
            if (object.contains("parent") && !json_validation::Unsigned(object["parent"], UINT32_MAX - 1)) return std::unexpected(path + "invalid parent");
            if (object.contains("name") && !object["name"].is_string()) return std::unexpected(path + "invalid name");
            if (object.contains("static") && !object["static"].is_boolean()) return std::unexpected(path + "invalid static");
            if (object.contains("transform"))
                if (auto valid = TransformData::ValidateJSON(object["transform"]); !valid)
                    return std::unexpected(path + "transform: " + valid.error());
            const auto entity = data.registry.create();
            data.idToEntity[id] = entity;
            data.maxID = std::max(data.maxID, id + 1);
            std::string name = object.value("name", std::string("Entity"));
            data.registry.emplace<GameObject>(entity, name, object.value("static", false));
            if (object.contains("transform")) data.registry.emplace<TransformData>(entity, object["transform"]);
            else data.registry.emplace<TransformData>(entity);
            const AssetHandle prefab = object.value("prefab", AssetHandle{0});
            if (prefab != 0)
                data.registry.emplace<PrefabReference>(entity, AssetRef<const SceneData>(prefab), object.contains("name"), object.contains("static"), object.contains("transform"));
            if (object.contains("components"))
            {
                if (!object["components"].is_array()) return std::unexpected(path + "components must be an array");
                std::unordered_set<std::string> keys;
                for (const auto &component : object["components"])
                {
                    if (!component.is_object() || !component.contains("type") || !component["type"].is_string())
                        return std::unexpected(path + "component requires a string type");
                    std::string key = component["type"];
                    if (key == "script" && component.contains("className") && component["className"].is_string())
                        key += ":" + component["className"].get<std::string>();
                    if (!keys.insert(key).second) return std::unexpected(path + "duplicate component " + key);
                    if (auto result = data.loadComponent(entity, component); !result)
                        return std::unexpected(path + component["type"].get<std::string>() + ": " + result.error());
                }
            }
        }
        for (const auto &object : json["objects"])
        {
            const auto id = object["id"].get<vke_ds::id32_t>();
            const auto parent = object.value("parent", vke_ds::id32_t{0});
            if (parent != 0 && !data.idToEntity.contains(parent)) return std::unexpected("entity " + std::to_string(id) + ": missing parent " + std::to_string(parent));
            data.parents[data.idToEntity.at(id)] = parent == 0 ? entt::null : data.idToEntity.at(parent);
        }
        const auto root = json.value("prefabRoot", vke_ds::id32_t{0});
        if (root != 0)
        {
            if (!data.idToEntity.contains(root)) return std::unexpected("missing prefabRoot entity");
            data.prefabRoot = data.idToEntity.at(root);
        }
        if (auto result = data.validateParsed(); !result) return std::unexpected(result.error());
        data.stage = SceneDataStage::Parsed;
        return data;
    }

    Result<void> SceneData::validateParsed() const
    {
        std::unordered_set<entt::entity> entities;
        size_t roots = 0;
        for (const auto &[id, entity] : idToEntity)
        {
            if (id == 0 || !registry.valid(entity) || !registry.all_of<GameObject, TransformData>(entity) || !entities.insert(entity).second)
                return std::unexpected("invalid entity mapping");
            const auto parent = parents.find(entity);
            if (parent == parents.end() || parent->second == entt::null) ++roots;
        }
        std::unordered_set<entt::entity> finished;
        for (const auto &[id, entity] : idToEntity)
        {
            std::unordered_set<entt::entity> path;
            auto current = entity;
            while (current != entt::null && !finished.contains(current))
            {
                if (!entities.contains(current)) return std::unexpected("entity " + std::to_string(id) + ": missing parent");
                if (!path.insert(current).second) return std::unexpected("entity " + std::to_string(id) + ": parent cycle");
                auto p = parents.find(current);
                current = p == parents.end() ? entt::null : p->second;
            }
            finished.insert(path.begin(), path.end());
        }
        if (prefabRoot != entt::null)
        {
            auto p = parents.find(prefabRoot);
            if (!entities.contains(prefabRoot) || roots != 1 || (p != parents.end() && p->second != entt::null))
                return std::unexpected("prefabRoot must identify the single root of the prefab");
        }
        return {};
    }

    Result<void> SceneData::ValidatePrefab() const
    {
        // validateParsed already checked any declared root and its hierarchy.
        if (prefabRoot == entt::null)
            return std::unexpected("prefabRoot must identify the single root of the prefab");
        return {};
    }

    Result<void> SceneData::ValidateExpanded() const
    {
        if (!registry.view<const PrefabReference>().empty())
            return std::unexpected("SceneData contains unexpanded prefab references");
        return {};
    }

    Result<void> SceneData::ValidateReady(bool requireSingleRoot) const
    {
        if (stage != SceneDataStage::Ready)
            return std::unexpected("SceneData must be Ready");
        if (requireSingleRoot && prefabRoot == entt::null)
        {
            size_t roots = 0;
            for (const auto &[id, entity] : idToEntity)
            {
                auto parent = parents.find(entity);
                if (parent == parents.end() || parent->second == entt::null) ++roots;
            }
            if (roots != 1) return std::unexpected("rootTransform requires a single root");
        }
        return {};
    }

    Result<nlohmann::json> SceneData::ToJSON() const
    {
        nlohmann::json result = {{"maxid", maxID}, {"objects", nlohmann::json::array()}};
        std::unordered_map<entt::entity, vke_ds::id32_t> ids;
        std::vector<vke_ds::id32_t> ordered;
        for (const auto &[id, entity] : idToEntity) { ids[entity] = id; ordered.push_back(id); }
        std::sort(ordered.begin(), ordered.end());
        if (prefabRoot != entt::null) result["prefabRoot"] = ids.at(prefabRoot);
        for (const auto id : ordered)
        {
            const auto entity = idToEntity.at(id);
            const auto &object = registry.get<GameObject>(entity);
            nlohmann::json value = {{"id", id}, {"parent", 0}, {"components", nlohmann::json::array()}};
            const auto *reference = registry.try_get<PrefabReference>(entity);
            if (!reference || reference->overrideName) value["name"] = object.name;
            if (!reference || reference->overrideStatic) value["static"] = object.isStatic;
            if (!reference || reference->overrideTransform) value["transform"] = registry.get<TransformData>(entity).ToJSON();
            if (reference) value["prefab"] = reference->scene.Handle();
            if (auto p = parents.find(entity); p != parents.end() && p->second != entt::null) value["parent"] = ids.at(p->second);
            if (auto components = componentToJSON(entity, value["components"]); !components)
                return std::unexpected("entity " + std::to_string(id) + ": " + components.error());
            result["objects"].push_back(std::move(value));
        }
        return result;
    }

    void SceneData::CopyComponents(entt::entity source, SceneData &target, entt::entity destination) const
    {
        using namespace vke_component;
        auto copy = [&]<typename... T>(std::tuple<T...> *)
        {
            ([&] { if (const auto *value = registry.try_get<T>(source)) target.registry.emplace_or_replace<T>(destination, *value); }(), ...);
        };
        copy(static_cast<std::tuple<TransformData, CameraData, RenderableObjectData, CanvasData,
            SkeletonAnimatorData, RigidBodyData, SensorData, CharacterControllerData, AudioSourceData,
            AudioListenerData, DirectionalLightData, PointLightData, SpotLightData, ScriptDataList, PrefabReference> *>(nullptr));
    }

    SceneData SceneData::Clone() const
    {
        SceneData result;
        result.stage = stage;
        result.maxID = maxID;
        std::unordered_map<entt::entity, entt::entity> mapping;
        for (const auto &[id, source] : idToEntity)
        {
            const auto target = result.registry.create();
            result.idToEntity[id] = target;
            mapping[source] = target;
            const auto &object = registry.get<GameObject>(source);
            auto name = object.name;
            result.registry.emplace<GameObject>(target, name, object.isStatic);
            CopyComponents(source, result, target);
        }
        for (const auto &[source, target] : mapping)
        {
            auto p = parents.find(source);
            result.parents[target] = p == parents.end() || p->second == entt::null ? entt::null : mapping.at(p->second);
        }
        if (prefabRoot != entt::null) result.prefabRoot = mapping.at(prefabRoot);
        return result;
    }
}
