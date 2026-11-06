#ifndef SCRIPT_COMPONENT_H
#define SCRIPT_COMPONENT_H

#include <json_validation.hpp>
#include <reflect/type_info.hpp>
#include <string>
#include <nlohmann/json.hpp>

namespace vke_component
{
    struct ScriptStateData
    {
        std::string className;
        vke_common::TypeInfoData data;

        static vke_common::Result<ScriptStateData> FromJSON(const nlohmann::json &json);
        vke_common::Result<nlohmann::json> ToJSON() const;

        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            auto result = Object(json).Require({"className", "data"}).Strings({"className"}).Result();
            if (!result)
                return result;
            if (json["className"].get<std::string>().empty() || !json["data"].is_object())
                return std::unexpected("script requires className and object data");
            return {};
        }
    };
}

#endif
