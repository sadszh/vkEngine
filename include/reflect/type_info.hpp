#ifndef VKE_REFLECT_TYPE_INFO_H
#define VKE_REFLECT_TYPE_INFO_H

#include <common.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace vke_common
{
    enum class TypeKind : uint8_t
    {
        Byte = 0,
        Int32 = 1,
        Int64 = 2,
        Float32 = 3,
        Float64 = 4,
        Vector = 5,
        String = 6,
        Array = 7,
        Struct = 8,
    };

    enum class TypeInfoError
    {
        InvalidTypeName,
        InvalidFieldName,
        DuplicateFieldName,
        NullReferencedType,
        InvalidScalarType,
        InvalidComponentCount,
    };

    constexpr std::string_view ToString(TypeInfoError error) noexcept
    {
        switch (error)
        {
        case TypeInfoError::InvalidTypeName:
            return "invalid type name";
        case TypeInfoError::InvalidFieldName:
            return "invalid field name";
        case TypeInfoError::DuplicateFieldName:
            return "duplicate field name";
        case TypeInfoError::NullReferencedType:
            return "null referenced type";
        case TypeInfoError::InvalidScalarType:
            return "invalid vector scalar type";
        case TypeInfoError::InvalidComponentCount:
            return "invalid vector component count";
        }

        return "unknown TypeInfo error";
    }

    enum class TypeInfoJsonErrorCode
    {
        TypeMismatch,
        MissingField,
        UnexpectedField,
        ElementCountMismatch,
        IntegerOutOfRange,
        NumberOutOfRange,
        InvalidUtf8,
        InvalidType,
        DepthLimitExceeded,
        ValueLimitExceeded,
        DataTooLarge,
        IntegerOverflow,
    };

    struct TypeInfoJsonError
    {
        TypeInfoJsonErrorCode code;
        std::string path;
    };

    constexpr std::string_view ToString(TypeInfoJsonErrorCode code) noexcept
    {
        switch (code)
        {
        case TypeInfoJsonErrorCode::TypeMismatch:
            return "JSON value has the wrong type";
        case TypeInfoJsonErrorCode::MissingField:
            return "JSON object is missing a field";
        case TypeInfoJsonErrorCode::UnexpectedField:
            return "JSON object contains an unexpected field";
        case TypeInfoJsonErrorCode::ElementCountMismatch:
            return "JSON array has the wrong element count";
        case TypeInfoJsonErrorCode::IntegerOutOfRange:
            return "JSON integer is out of range";
        case TypeInfoJsonErrorCode::NumberOutOfRange:
            return "JSON number is out of range";
        case TypeInfoJsonErrorCode::InvalidUtf8:
            return "JSON string is not valid UTF-8";
        case TypeInfoJsonErrorCode::InvalidType:
            return "type information is invalid";
        case TypeInfoJsonErrorCode::DepthLimitExceeded:
            return "value nesting depth limit exceeded";
        case TypeInfoJsonErrorCode::ValueLimitExceeded:
            return "value limit exceeded";
        case TypeInfoJsonErrorCode::DataTooLarge:
            return "encoded data is too large";
        case TypeInfoJsonErrorCode::IntegerOverflow:
            return "integer overflow";
        }

        return "unknown TypeInfo JSON error";
    }

    template <typename T>
    using TypeInfoJsonResult = Result<T, TypeInfoJsonError>;

    using TypeInfoData = std::vector<std::byte>;
    using TypeInfoDataPtr = std::unique_ptr<TypeInfoData>;
    using EncodeBinaryResult = TypeInfoJsonResult<TypeInfoDataPtr>;

    class TypeInfo;
    using TypeInfoPtr = std::shared_ptr<const TypeInfo>;
    using TypeInfoResult = Result<TypeInfoPtr, TypeInfoError>;
    using DecodeTypeInfoJsonResult = TypeInfoJsonResult<TypeInfoPtr>;

    struct FieldInfo
    {
        std::string name;
        TypeInfoPtr type;
    };

    struct ByteTypeInfo
    {
    };

    struct Int32TypeInfo
    {
    };

    struct Int64TypeInfo
    {
    };

    struct Float32TypeInfo
    {
    };

    struct Float64TypeInfo
    {
    };

    struct VectorTypeInfo
    {
        TypeInfoPtr scalarType;
        uint32_t componentCount;
    };

    struct StringTypeInfo
    {
    };

    struct ArrayTypeInfo
    {
        TypeInfoPtr elementType;
    };

    struct StructTypeInfo
    {
        std::vector<FieldInfo> fields;
    };

    // The alternative order intentionally matches TypeKind's serialized values.
    using TypeInfoPayload = std::variant<
        ByteTypeInfo,
        Int32TypeInfo,
        Int64TypeInfo,
        Float32TypeInfo,
        Float64TypeInfo,
        VectorTypeInfo,
        StringTypeInfo,
        ArrayTypeInfo,
        StructTypeInfo>;

    static_assert(std::variant_size_v<TypeInfoPayload> == 9);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Byte), TypeInfoPayload>, ByteTypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Int32), TypeInfoPayload>, Int32TypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Int64), TypeInfoPayload>, Int64TypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Float32), TypeInfoPayload>, Float32TypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Float64), TypeInfoPayload>, Float64TypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Vector), TypeInfoPayload>, VectorTypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::String), TypeInfoPayload>, StringTypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Array), TypeInfoPayload>, ArrayTypeInfo>);
    static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(TypeKind::Struct), TypeInfoPayload>, StructTypeInfo>);

    class TypeInfo final
    {
    public:
        static constexpr uint32_t MaxNameByteLength = 1024;

        static const TypeInfoPtr &Byte() noexcept;

        static const TypeInfoPtr &Int32() noexcept;

        static const TypeInfoPtr &Int64() noexcept;

        static const TypeInfoPtr &Float32() noexcept;

        static const TypeInfoPtr &Float64() noexcept;

        static const TypeInfoPtr &String() noexcept;

        static TypeInfoResult CreateVector(TypeInfoPtr scalarType, uint32_t componentCount,
                                           std::string name = {});

        static TypeInfoResult CreateArray(TypeInfoPtr elementType, std::string name = {});

        static TypeInfoResult CreateStruct(std::string name, std::vector<FieldInfo> fields);

        // Deserializes a recursive TypeInfo definition from JSON.
        static DecodeTypeInfoJsonResult FromJson(const nlohmann::json &json);

        // Encodes a JSON representation of this type into the binary Data format.
        EncodeBinaryResult EncodeBinaryFromJson(const nlohmann::json &json) const;

        const std::string &Name() const noexcept { return name; }
        TypeKind Kind() const noexcept { return static_cast<TypeKind>(payload.index()); }
        uint32_t Alignment() const noexcept { return alignment; }
        const TypeInfoPayload &Payload() const noexcept { return payload; }

        template <typename T>
        bool Is() const noexcept
        {
            static_assert(IsPayloadAlternative<T>);
            return std::holds_alternative<T>(payload);
        }

        template <typename T>
        const T *GetIf() const noexcept
        {
            static_assert(IsPayloadAlternative<T>);
            return std::get_if<T>(&payload);
        }

    private:
        std::string name;
        uint32_t alignment;
        TypeInfoPayload payload;

        template <typename T>
        static constexpr bool IsPayloadAlternative =
            std::disjunction_v<std::is_same<T, ByteTypeInfo>,
                               std::is_same<T, Int32TypeInfo>,
                               std::is_same<T, Int64TypeInfo>,
                               std::is_same<T, Float32TypeInfo>,
                               std::is_same<T, Float64TypeInfo>,
                               std::is_same<T, VectorTypeInfo>,
                               std::is_same<T, StringTypeInfo>,
                               std::is_same<T, ArrayTypeInfo>,
                               std::is_same<T, StructTypeInfo>>;

        template <typename T>
        TypeInfo(std::string name, uint32_t alignment, T payload)
            : name(std::move(name)), alignment(alignment), payload(std::move(payload))
        {
            static_assert(IsPayloadAlternative<T>);
        }

        static bool isValidName(std::string_view name) noexcept
        {
            return !name.empty() && name.size() <= MaxNameByteLength;
        }

        static bool isVectorScalar(TypeKind kind) noexcept
        {
            return kind == TypeKind::Int32 || kind == TypeKind::Int64 ||
                   kind == TypeKind::Float32 || kind == TypeKind::Float64;
        }

    };
}

#endif
