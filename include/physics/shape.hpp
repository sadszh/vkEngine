#ifndef PHYSICS_SHAPE_H
#define PHYSICS_SHAPE_H

#include <json_validation.hpp>
#include <physics/physics.hpp>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/TriangleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <glm/vec3.hpp>

namespace vke_physics
{
    enum PhyscisShapeType
    {
        PHYSICS_SHAPE_SPHERE,
        PHYSICS_SHAPE_BOX,
        PHYSICS_SHAPE_CAPSULE,
        PHYSICS_SHAPE_CYLINDER,
        PHYSICS_SHAPE_TRIANGLE,
        PHYSICS_SHAPE_PLANE,
    };

    struct PhyscisShapeData
    {
        PhyscisShapeType type = PHYSICS_SHAPE_BOX;
        float radius = 0.5f;
        float halfHeight = 0.5f;
        glm::vec3 halfExtent{0.5f};
        glm::vec3 v1{0.0f};
        glm::vec3 v2{0.0f};
        glm::vec3 v3{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float c = 0.0f;

        // Call before constructing from JSON; asset checks happen in LoadAssets where applicable.
        static vke_common::Result<void> ValidateJSON(const nlohmann::json &json)
        {
            using namespace vke_common::json_validation;
            if (!json.is_object() || !json.contains("type") || !Unsigned(json["type"], 5))
                return std::unexpected("invalid physics shape type");
            const int type = json["type"].get<int>();
            for (const auto *key : {"radius", "halfHeight", "c"})
                if (json.contains(key) && !Number(json[key])) return std::unexpected(std::string("invalid shape ") + key);
            for (const auto *key : {"halfExtent", "v1", "v2", "v3", "normal"})
                if (json.contains(key) && !Vector(json[key], 3)) return std::unexpected(std::string("invalid shape ") + key);
            if ((type == 0 || type == 2 || type == 3) && (!json.contains("radius") || json["radius"].get<float>() <= 0))
                return std::unexpected("shape requires positive radius");
            if ((type == 2 || type == 3) && (!json.contains("halfHeight") || json["halfHeight"].get<float>() < 0))
                return std::unexpected("shape requires nonnegative halfHeight");
            if (type == 1 && (!json.contains("halfExtent") || std::any_of(json["halfExtent"].begin(), json["halfExtent"].end(), [](const auto &x) { return x.template get<float>() <= 0; })))
                return std::unexpected("box requires positive halfExtent");
            if (type == 4 && (!json.contains("v1") || !json.contains("v2") || !json.contains("v3")))
                return std::unexpected("triangle requires v1, v2 and v3");
            if (type == 5 && (!json.contains("normal") || !json.contains("c")))
                return std::unexpected("plane requires normal and c");
            return {};
        }

        PhyscisShapeData() = default;
        PhyscisShapeData(const nlohmann::json &json)
            : type(json["type"])
        {
            switch (type)
            {
            case PHYSICS_SHAPE_SPHERE:
                radius = json["radius"];
                break;
            case PHYSICS_SHAPE_BOX:
                halfExtent = glm::vec3(
                    json["halfExtent"][0].get<float>(),
                    json["halfExtent"][1].get<float>(),
                    json["halfExtent"][2].get<float>());
                break;
            case PHYSICS_SHAPE_CAPSULE:
            case PHYSICS_SHAPE_CYLINDER:
                halfHeight = json["halfHeight"];
                radius = json["radius"];
                break;
            case PHYSICS_SHAPE_TRIANGLE:
                v1 = glm::vec3(
                    json["v1"][0].get<float>(),
                    json["v1"][1].get<float>(),
                    json["v1"][2].get<float>());
                v2 = glm::vec3(
                    json["v2"][0].get<float>(),
                    json["v2"][1].get<float>(),
                    json["v2"][2].get<float>());
                v3 = glm::vec3(
                    json["v3"][0].get<float>(),
                    json["v3"][1].get<float>(),
                    json["v3"][2].get<float>());
                break;
            case PHYSICS_SHAPE_PLANE:
                normal = glm::vec3(
                    json["normal"][0].get<float>(),
                    json["normal"][1].get<float>(),
                    json["normal"][2].get<float>());
                c = json["c"];
                break;
            default:
                break;
            }
        }

        nlohmann::json ToJSON() const
        {
            nlohmann::json json = {{"type", static_cast<int>(type)}};
            switch (type)
            {
            case PHYSICS_SHAPE_SPHERE:
                json["radius"] = radius;
                break;
            case PHYSICS_SHAPE_BOX:
                json["halfExtent"] = {halfExtent.x, halfExtent.y, halfExtent.z};
                break;
            case PHYSICS_SHAPE_CAPSULE:
            case PHYSICS_SHAPE_CYLINDER:
                json["radius"] = radius;
                json["halfHeight"] = halfHeight;
                break;
            case PHYSICS_SHAPE_TRIANGLE:
                json["v1"] = {v1.x, v1.y, v1.z};
                json["v2"] = {v2.x, v2.y, v2.z};
                json["v3"] = {v3.x, v3.y, v3.z};
                break;
            case PHYSICS_SHAPE_PLANE:
                json["normal"] = {normal.x, normal.y, normal.z};
                json["c"] = c;
                break;
            default:
                break;
            }
            return json;
        }
    };

    class PhyscisShape
    {
    public:
        PhyscisShapeType type = PHYSICS_SHAPE_BOX;
        JPH::ShapeRefC shapeRef;

        PhyscisShape() : shapeRef(nullptr) {}
        PhyscisShape(PhyscisShapeType type) : type(type), shapeRef(nullptr) {}

        explicit PhyscisShape(const JPH::Shape *shape) : shapeRef(shape)
        {
            VKE_FATAL_IF(shape == nullptr, "Physics shape is null")
            switch (shape->GetSubType())
            {
            case JPH::EShapeSubType::Sphere: type = PHYSICS_SHAPE_SPHERE; break;
            case JPH::EShapeSubType::Box: type = PHYSICS_SHAPE_BOX; break;
            case JPH::EShapeSubType::Capsule: type = PHYSICS_SHAPE_CAPSULE; break;
            case JPH::EShapeSubType::Cylinder: type = PHYSICS_SHAPE_CYLINDER; break;
            case JPH::EShapeSubType::Triangle: type = PHYSICS_SHAPE_TRIANGLE; break;
            case JPH::EShapeSubType::Plane: type = PHYSICS_SHAPE_PLANE; break;
            default:
                VKE_FATAL("Unsupported physics shape subtype {}", static_cast<int>(shape->GetSubType()))
            }
        }

        PhyscisShape(const PhyscisShapeData &data) : type(data.type)
        {
            switch (type)
            {
            case PHYSICS_SHAPE_SPHERE:
                shapeRef = new JPH::SphereShape(data.radius);
                break;
            case PHYSICS_SHAPE_BOX:
                shapeRef = new JPH::BoxShape(
                    JPH::Vec3Arg(data.halfExtent.x, data.halfExtent.y, data.halfExtent.z));
                break;
            case PHYSICS_SHAPE_CAPSULE:
                shapeRef = new JPH::CapsuleShape(data.halfHeight, data.radius);
                break;
            case PHYSICS_SHAPE_CYLINDER:
                shapeRef = new JPH::CylinderShape(data.halfHeight, data.radius);
                break;
            case PHYSICS_SHAPE_TRIANGLE:
                shapeRef = new JPH::TriangleShape(
                    JPH::Vec3Arg(data.v1.x, data.v1.y, data.v1.z),
                    JPH::Vec3Arg(data.v2.x, data.v2.y, data.v2.z),
                    JPH::Vec3Arg(data.v3.x, data.v3.y, data.v3.z));
                break;
            case PHYSICS_SHAPE_PLANE:
                shapeRef = new JPH::PlaneShape(
                    JPH::Plane(JPH::Vec3Arg(data.normal.x, data.normal.y, data.normal.z), data.c));
                break;
            default:
                break;
            }
        }

        void FillData(PhyscisShapeData &data) const
        {
            data = PhyscisShapeData{};
            data.type = type;
            if (shapeRef == nullptr)
                return;

            switch (type)
            {
            case PHYSICS_SHAPE_SPHERE:
                data.radius =
                    static_cast<const JPH::SphereShape *>(shapeRef.GetPtr())->GetRadius();
                break;
            case PHYSICS_SHAPE_BOX:
            {
                const JPH::Vec3 extent =
                    static_cast<const JPH::BoxShape *>(shapeRef.GetPtr())->GetHalfExtent();
                data.halfExtent = {extent.GetX(), extent.GetY(), extent.GetZ()};
                break;
            }
            case PHYSICS_SHAPE_CAPSULE:
            {
                const auto *capsule =
                    static_cast<const JPH::CapsuleShape *>(shapeRef.GetPtr());
                data.radius = capsule->GetRadius();
                data.halfHeight = capsule->GetHalfHeightOfCylinder();
                break;
            }
            case PHYSICS_SHAPE_CYLINDER:
            {
                const auto *cylinder =
                    static_cast<const JPH::CylinderShape *>(shapeRef.GetPtr());
                data.radius = cylinder->GetRadius();
                data.halfHeight = cylinder->GetHalfHeight();
                break;
            }
            case PHYSICS_SHAPE_TRIANGLE:
            {
                const auto *triangle =
                    static_cast<const JPH::TriangleShape *>(shapeRef.GetPtr());
                const JPH::Vec3 vertex1 = triangle->GetVertex1();
                const JPH::Vec3 vertex2 = triangle->GetVertex2();
                const JPH::Vec3 vertex3 = triangle->GetVertex3();
                data.v1 = {vertex1.GetX(), vertex1.GetY(), vertex1.GetZ()};
                data.v2 = {vertex2.GetX(), vertex2.GetY(), vertex2.GetZ()};
                data.v3 = {vertex3.GetX(), vertex3.GetY(), vertex3.GetZ()};
                break;
            }
            case PHYSICS_SHAPE_PLANE:
            {
                const JPH::Plane plane =
                    static_cast<const JPH::PlaneShape *>(shapeRef.GetPtr())->GetPlane();
                const JPH::Vec3 normal = plane.GetNormal();
                data.normal = {normal.GetX(), normal.GetY(), normal.GetZ()};
                data.c = plane.GetConstant();
                break;
            }
            default:
                break;
            }
        }
    };

}

#endif
