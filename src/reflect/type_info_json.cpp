#include <reflect/type_info.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace vke_common
{
    namespace
    {
        constexpr uint32_t MaxDepth = 64;
        constexpr uint32_t MaxStructFieldCount = 4'096;
        constexpr uint32_t MaxStringByteLength = 256u * 1024u * 1024u;
        constexpr uint32_t MaxArrayElementCount = 16'777'216;
        constexpr uint32_t MaxByteArrayElementCount = 1'073'741'824;
        constexpr uint64_t MaxDataSize = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());

        struct EncodeContext
        {
            TypeInfoData *output = nullptr;
        };

        using EncodeResult = TypeInfoJsonResult<uint64_t>;

        constexpr uint32_t MaxTypeInfoDepth = 64;

        std::unexpected<TypeInfoJsonError> JsonError(TypeInfoJsonErrorCode code,
                                                      const std::string &path)
        {
            return std::unexpected(TypeInfoJsonError{code, path});
        }

        bool IsAllowedField(std::string_view field,
                            std::initializer_list<std::string_view> allowed)
        {
            return std::find(allowed.begin(), allowed.end(), field) != allowed.end();
        }

        bool IsValidUtf8(std::string_view bytes) noexcept;

        TypeInfoJsonResult<std::string> ReadRequiredString(
            const nlohmann::json &json, std::string_view field, const std::string &path)
        {
            const auto value = json.find(field);
            const std::string fieldPath = path + "." + std::string(field);
            if (value == json.end())
                return JsonError(TypeInfoJsonErrorCode::MissingField, fieldPath);
            if (!value->is_string())
                return JsonError(TypeInfoJsonErrorCode::TypeMismatch, fieldPath);
            const auto &text = value->get_ref<const std::string &>();
            if (!IsValidUtf8(text))
                return JsonError(TypeInfoJsonErrorCode::InvalidUtf8, fieldPath);
            return text;
        }

        TypeInfoJsonResult<std::string> ReadOptionalName(
            const nlohmann::json &json, const std::string &path)
        {
            const auto value = json.find("name");
            if (value == json.end())
                return std::string{};
            if (!value->is_string())
                return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path + ".name");
            const auto &name = value->get_ref<const std::string &>();
            if (!IsValidUtf8(name))
                return JsonError(TypeInfoJsonErrorCode::InvalidUtf8, path + ".name");
            if (name.empty())
                return JsonError(TypeInfoJsonErrorCode::InvalidType, path + ".name");
            return name;
        }

        std::optional<TypeInfoJsonError> FindUnexpectedField(
            const nlohmann::json &json, std::initializer_list<std::string_view> allowed,
            const std::string &path)
        {
            for (auto value = json.begin(); value != json.end(); ++value)
            {
                if (!IsAllowedField(value.key(), allowed))
                    return TypeInfoJsonError{TypeInfoJsonErrorCode::UnexpectedField,
                                             path + "." + value.key()};
            }
            return std::nullopt;
        }

        DecodeTypeInfoJsonResult DecodeTypeInfo(const nlohmann::json &json,
                                                 uint32_t depth,
                                                 const std::string &path)
        {
            if (depth > MaxTypeInfoDepth)
                return JsonError(TypeInfoJsonErrorCode::DepthLimitExceeded, path);
            if (!json.is_object())
                return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);

            auto kind = ReadRequiredString(json, "kind", path);
            if (!kind)
                return std::unexpected(kind.error());

            auto primitive = [&](const TypeInfoPtr &type) -> DecodeTypeInfoJsonResult
            {
                if (auto error = FindUnexpectedField(json, {"kind"}, path))
                    return std::unexpected(std::move(*error));
                return type;
            };
            if (*kind == "byte") return primitive(TypeInfo::Byte());
            if (*kind == "int32") return primitive(TypeInfo::Int32());
            if (*kind == "int64") return primitive(TypeInfo::Int64());
            if (*kind == "float32") return primitive(TypeInfo::Float32());
            if (*kind == "float64") return primitive(TypeInfo::Float64());
            if (*kind == "string") return primitive(TypeInfo::String());

            if (*kind == "vector")
            {
                if (auto error = FindUnexpectedField(
                        json, {"kind", "name", "scalarType", "componentCount"}, path))
                    return std::unexpected(std::move(*error));
                const auto scalarJson = json.find("scalarType");
                if (scalarJson == json.end())
                    return JsonError(TypeInfoJsonErrorCode::MissingField, path + ".scalarType");
                auto scalar = DecodeTypeInfo(*scalarJson, depth + 1, path + ".scalarType");
                if (!scalar)
                    return std::unexpected(scalar.error());

                const auto countJson = json.find("componentCount");
                if (countJson == json.end())
                    return JsonError(TypeInfoJsonErrorCode::MissingField,
                                     path + ".componentCount");
                if (!countJson->is_number_unsigned() && !countJson->is_number_integer())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch,
                                     path + ".componentCount");
                uint64_t count = 0;
                if (countJson->is_number_unsigned())
                    count = countJson->get<uint64_t>();
                else
                {
                    const int64_t signedCount = countJson->get<int64_t>();
                    if (signedCount < 0)
                        return JsonError(TypeInfoJsonErrorCode::IntegerOutOfRange,
                                         path + ".componentCount");
                    count = static_cast<uint64_t>(signedCount);
                }
                if (count > std::numeric_limits<uint32_t>::max())
                    return JsonError(TypeInfoJsonErrorCode::IntegerOutOfRange,
                                     path + ".componentCount");
                auto name = ReadOptionalName(json, path);
                if (!name)
                    return std::unexpected(name.error());
                auto result = TypeInfo::CreateVector(
                    std::move(*scalar), static_cast<uint32_t>(count), std::move(*name));
                if (!result)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                return std::move(*result);
            }

            if (*kind == "array")
            {
                if (auto error = FindUnexpectedField(
                        json, {"kind", "name", "elementType"}, path))
                    return std::unexpected(std::move(*error));
                const auto elementJson = json.find("elementType");
                if (elementJson == json.end())
                    return JsonError(TypeInfoJsonErrorCode::MissingField, path + ".elementType");
                auto element = DecodeTypeInfo(*elementJson, depth + 1, path + ".elementType");
                if (!element)
                    return std::unexpected(element.error());
                auto name = ReadOptionalName(json, path);
                if (!name)
                    return std::unexpected(name.error());
                auto result = TypeInfo::CreateArray(std::move(*element), std::move(*name));
                if (!result)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                return std::move(*result);
            }

            if (*kind == "struct")
            {
                if (auto error = FindUnexpectedField(json, {"kind", "name", "fields"}, path))
                    return std::unexpected(std::move(*error));
                auto name = ReadRequiredString(json, "name", path);
                if (!name)
                    return std::unexpected(name.error());
                const auto fieldsJson = json.find("fields");
                if (fieldsJson == json.end())
                    return JsonError(TypeInfoJsonErrorCode::MissingField, path + ".fields");
                if (!fieldsJson->is_array())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path + ".fields");
                if (fieldsJson->size() > MaxStructFieldCount)
                    return JsonError(TypeInfoJsonErrorCode::ValueLimitExceeded,
                                     path + ".fields");

                std::vector<FieldInfo> fields;
                fields.reserve(fieldsJson->size());
                for (std::size_t i = 0; i < fieldsJson->size(); ++i)
                {
                    const auto &fieldJson = (*fieldsJson)[i];
                    const std::string fieldPath = path + ".fields[" + std::to_string(i) + "]";
                    if (!fieldJson.is_object())
                        return JsonError(TypeInfoJsonErrorCode::TypeMismatch, fieldPath);
                    if (auto error = FindUnexpectedField(fieldJson, {"name", "type"}, fieldPath))
                        return std::unexpected(std::move(*error));
                    auto fieldName = ReadRequiredString(fieldJson, "name", fieldPath);
                    if (!fieldName)
                        return std::unexpected(fieldName.error());
                    const auto fieldTypeJson = fieldJson.find("type");
                    if (fieldTypeJson == fieldJson.end())
                        return JsonError(TypeInfoJsonErrorCode::MissingField,
                                         fieldPath + ".type");
                    auto fieldType = DecodeTypeInfo(*fieldTypeJson, depth + 1,
                                                    fieldPath + ".type");
                    if (!fieldType)
                        return std::unexpected(fieldType.error());
                    fields.push_back(FieldInfo{std::move(*fieldName), std::move(*fieldType)});
                }
                auto result = TypeInfo::CreateStruct(std::move(*name), std::move(fields));
                if (!result)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                return std::move(*result);
            }

            return JsonError(TypeInfoJsonErrorCode::InvalidType, path + ".kind");
        }

        std::string FieldPath(const std::string &path, std::string_view field)
        {
            std::string result;
            result.reserve(path.size() + field.size() + 1);
            result.append(path);
            result.push_back('.');
            result.append(field);
            return result;
        }

        std::string ElementPath(const std::string &path, std::size_t index)
        {
            return path + '[' + std::to_string(index) + ']';
        }

        bool IsValidUtf8(std::string_view bytes) noexcept
        {
            std::size_t i = 0;
            while (i < bytes.size())
            {
                const uint8_t first = static_cast<uint8_t>(bytes[i]);
                if (first <= 0x7f)
                {
                    ++i;
                    continue;
                }

                std::size_t length;
                uint8_t secondMin = 0x80;
                uint8_t secondMax = 0xbf;
                if (first >= 0xc2 && first <= 0xdf)
                {
                    length = 2;
                }
                else if (first >= 0xe0 && first <= 0xef)
                {
                    length = 3;
                    if (first == 0xe0)
                        secondMin = 0xa0;
                    else if (first == 0xed)
                        secondMax = 0x9f;
                }
                else if (first >= 0xf0 && first <= 0xf4)
                {
                    length = 4;
                    if (first == 0xf0)
                        secondMin = 0x90;
                    else if (first == 0xf4)
                        secondMax = 0x8f;
                }
                else
                {
                    return false;
                }

                if (length > bytes.size() - i)
                    return false;
                const uint8_t second = static_cast<uint8_t>(bytes[i + 1]);
                if (second < secondMin || second > secondMax)
                    return false;
                for (std::size_t j = 2; j < length; ++j)
                {
                    const uint8_t continuation = static_cast<uint8_t>(bytes[i + j]);
                    if (continuation < 0x80 || continuation > 0xbf)
                        return false;
                }
                i += length;
            }
            return true;
        }

        EncodeResult Align(uint64_t cursor, uint32_t alignment, const std::string &path)
        {
            if (alignment == 0)
                return JsonError(TypeInfoJsonErrorCode::InvalidType, path);

            const uint64_t remainder = cursor % alignment;
            if (remainder != 0)
                cursor += alignment - remainder;
            if (cursor > MaxDataSize)
                return JsonError(TypeInfoJsonErrorCode::DataTooLarge, path);
            return cursor;
        }

        EncodeResult Advance(uint64_t cursor, uint64_t size, const std::string &path)
        {
            if (size > MaxDataSize || cursor > MaxDataSize - size)
                return JsonError(TypeInfoJsonErrorCode::DataTooLarge, path);
            return cursor + size;
        }

        template <typename T>
        void WriteLittle(TypeInfoData &output, uint64_t offset, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = static_cast<Unsigned>(value);
            for (std::size_t i = 0; i < sizeof(T); ++i)
                output[static_cast<std::size_t>(offset) + i] =
                    static_cast<std::byte>((bits >> (i * 8)) & 0xffu);
        }

        void WriteFloat32(TypeInfoData &output, uint64_t offset, float value)
        {
            WriteLittle(output, offset, std::bit_cast<uint32_t>(value));
        }

        void WriteFloat64(TypeInfoData &output, uint64_t offset, double value)
        {
            WriteLittle(output, offset, std::bit_cast<uint64_t>(value));
        }

        TypeInfoJsonResult<int64_t> ReadInteger(
            const nlohmann::json &json, int64_t minimum, int64_t maximum,
            const std::string &path)
        {
            if (json.is_number_unsigned())
            {
                const uint64_t value = json.get<uint64_t>();
                if (value > static_cast<uint64_t>(maximum))
                    return JsonError(TypeInfoJsonErrorCode::IntegerOutOfRange, path);
                return static_cast<int64_t>(value);
            }
            if (!json.is_number_integer())
                return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);

            const int64_t value = json.get<int64_t>();
            if (value < minimum || value > maximum)
                return JsonError(TypeInfoJsonErrorCode::IntegerOutOfRange, path);
            return value;
        }

        EncodeResult EncodeValue(EncodeContext &context, const TypeInfo &type,
                                 const nlohmann::json &json, uint64_t cursor,
                                 uint32_t depth, const std::string &path)
        {
            if (depth > MaxDepth)
                return JsonError(TypeInfoJsonErrorCode::DepthLimitExceeded, path);

            auto aligned = Align(cursor, type.Alignment(), path);
            if (!aligned)
                return std::unexpected(aligned.error());
            cursor = *aligned;

            switch (type.Kind())
            {
            case TypeKind::Byte:
            {
                auto value = ReadInteger(json, 0, std::numeric_limits<uint8_t>::max(), path);
                if (!value)
                    return std::unexpected(value.error());
                auto end = Advance(cursor, 1, path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                    (*context.output)[static_cast<std::size_t>(cursor)] =
                        static_cast<std::byte>(*value);
                return *end;
            }
            case TypeKind::Int32:
            {
                auto value = ReadInteger(json, std::numeric_limits<int32_t>::min(),
                                         std::numeric_limits<int32_t>::max(), path);
                if (!value)
                    return std::unexpected(value.error());
                auto end = Advance(cursor, sizeof(int32_t), path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                    WriteLittle(*context.output, cursor, static_cast<int32_t>(*value));
                return *end;
            }
            case TypeKind::Int64:
            {
                auto value = ReadInteger(json, std::numeric_limits<int64_t>::min(),
                                         std::numeric_limits<int64_t>::max(), path);
                if (!value)
                    return std::unexpected(value.error());
                auto end = Advance(cursor, sizeof(int64_t), path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                    WriteLittle(*context.output, cursor, *value);
                return *end;
            }
            case TypeKind::Float32:
            {
                if (!json.is_number())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);
                const double value = json.get<double>();
                if (!std::isfinite(value) ||
                    value < -static_cast<double>(std::numeric_limits<float>::max()) ||
                    value > static_cast<double>(std::numeric_limits<float>::max()))
                    return JsonError(TypeInfoJsonErrorCode::NumberOutOfRange, path);
                auto end = Advance(cursor, sizeof(float), path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                    WriteFloat32(*context.output, cursor, static_cast<float>(value));
                return *end;
            }
            case TypeKind::Float64:
            {
                if (!json.is_number())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);
                const double value = json.get<double>();
                if (!std::isfinite(value))
                    return JsonError(TypeInfoJsonErrorCode::NumberOutOfRange, path);
                auto end = Advance(cursor, sizeof(double), path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                    WriteFloat64(*context.output, cursor, value);
                return *end;
            }
            case TypeKind::Vector:
            {
                const auto *info = type.GetIf<VectorTypeInfo>();
                if (!info || !info->scalarType || info->componentCount < 2 ||
                    info->componentCount > 4)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                const TypeKind scalarKind = info->scalarType->Kind();
                if (scalarKind != TypeKind::Int32 && scalarKind != TypeKind::Int64 &&
                    scalarKind != TypeKind::Float32 && scalarKind != TypeKind::Float64)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                if (!json.is_array())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);
                if (json.size() != info->componentCount)
                    return JsonError(TypeInfoJsonErrorCode::ElementCountMismatch, path);

                for (std::size_t i = 0; i < json.size(); ++i)
                {
                    auto end = EncodeValue(context, *info->scalarType, json[i], cursor,
                                           depth, ElementPath(path, i));
                    if (!end)
                        return std::unexpected(end.error());
                    cursor = *end;
                }
                return cursor;
            }
            case TypeKind::String:
            {
                if (!json.is_string())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);
                const auto &value = json.get_ref<const std::string &>();
                if (value.size() > MaxStringByteLength)
                    return JsonError(TypeInfoJsonErrorCode::ValueLimitExceeded, path);
                if (!IsValidUtf8(value))
                    return JsonError(TypeInfoJsonErrorCode::InvalidUtf8, path);

                auto end = Advance(cursor, sizeof(uint32_t) + value.size(), path);
                if (!end)
                    return std::unexpected(end.error());
                if (context.output)
                {
                    WriteLittle(*context.output, cursor, static_cast<uint32_t>(value.size()));
                    if (!value.empty())
                        std::memcpy(context.output->data() + static_cast<std::size_t>(cursor) +
                                        sizeof(uint32_t),
                                    value.data(), value.size());
                }
                return *end;
            }
            case TypeKind::Array:
            {
                const auto *info = type.GetIf<ArrayTypeInfo>();
                if (!info || !info->elementType)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                if (!json.is_array())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);
                const uint32_t maxCount = info->elementType->Kind() == TypeKind::Byte
                                              ? MaxByteArrayElementCount
                                              : MaxArrayElementCount;
                if (json.size() > maxCount)
                    return JsonError(TypeInfoJsonErrorCode::ValueLimitExceeded, path);

                auto afterCount = Advance(cursor, sizeof(uint32_t), path);
                if (!afterCount)
                    return std::unexpected(afterCount.error());
                if (context.output)
                    WriteLittle(*context.output, cursor, static_cast<uint32_t>(json.size()));
                cursor = *afterCount;

                auto dataStart = Align(cursor, info->elementType->Alignment(), path);
                if (!dataStart)
                    return std::unexpected(dataStart.error());
                cursor = *dataStart;

                for (std::size_t i = 0; i < json.size(); ++i)
                {
                    auto end = EncodeValue(context, *info->elementType, json[i], cursor,
                                           depth + 1, ElementPath(path, i));
                    if (!end)
                        return std::unexpected(end.error());
                    cursor = *end;
                }
                return cursor;
            }
            case TypeKind::Struct:
            {
                const auto *info = type.GetIf<StructTypeInfo>();
                if (!info)
                    return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
                if (info->fields.size() > MaxStructFieldCount)
                    return JsonError(TypeInfoJsonErrorCode::ValueLimitExceeded, path);
                if (!json.is_object())
                    return JsonError(TypeInfoJsonErrorCode::TypeMismatch, path);

                for (const auto &field : info->fields)
                {
                    if (!field.type)
                        return JsonError(TypeInfoJsonErrorCode::InvalidType,
                                         FieldPath(path, field.name));
                    const auto value = json.find(field.name);
                    if (value == json.end())
                        return JsonError(TypeInfoJsonErrorCode::MissingField,
                                         FieldPath(path, field.name));
                    auto end = EncodeValue(context, *field.type, *value, cursor,
                                           depth + 1, FieldPath(path, field.name));
                    if (!end)
                        return std::unexpected(end.error());
                    cursor = *end;
                }

                if (json.size() != info->fields.size())
                {
                    for (auto value = json.begin(); value != json.end(); ++value)
                    {
                        bool known = false;
                        for (const auto &field : info->fields)
                        {
                            if (field.name == value.key())
                            {
                                known = true;
                                break;
                            }
                        }
                        if (!known)
                            return JsonError(TypeInfoJsonErrorCode::UnexpectedField,
                                             FieldPath(path, value.key()));
                    }
                }

                return Align(cursor, type.Alignment(), path);
            }
            }

            return JsonError(TypeInfoJsonErrorCode::InvalidType, path);
        }
    }

    DecodeTypeInfoJsonResult TypeInfo::FromJson(const nlohmann::json &json)
    {
        return DecodeTypeInfo(json, 1, "$");
    }

    EncodeBinaryResult TypeInfo::EncodeBinaryFromJson(const nlohmann::json &json) const
    {
        EncodeContext measureContext;
        auto size = EncodeValue(measureContext, *this, json, 0, 1, "$");
        if (!size)
            return std::unexpected(size.error());

        auto output = std::make_unique<TypeInfoData>(static_cast<std::size_t>(*size),
                                                     std::byte{0});
        EncodeContext writeContext{.output = output.get()};
        auto written = EncodeValue(writeContext, *this, json, 0, 1, "$");
        if (!written)
            return std::unexpected(written.error());
        if (*written != *size)
            return JsonError(TypeInfoJsonErrorCode::IntegerOverflow, "$");
        return output;
    }
}
