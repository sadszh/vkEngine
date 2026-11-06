#include <reflect/value_view.hpp>

#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace vke_common
{
    nlohmann::json ValueView::ToJSON() const
    {
        switch (Type()->Kind())
        {
        case TypeKind::Byte: return *AsByte();
        case TypeKind::Int32: return *AsInt32();
        case TypeKind::Int64: return *AsInt64();
        case TypeKind::Float32: return *AsFloat32();
        case TypeKind::Float64: return *AsFloat64();
        case TypeKind::String: return std::string(*AsString());
        case TypeKind::Struct:
        {
            auto result = nlohmann::json::object();
            const auto &fields = Type()->GetIf<StructTypeInfo>()->fields;
            for (std::size_t i = 0; i < fields.size(); ++i)
                result[fields[i].name] = Field(i)->ToJSON();
            return result;
        }
        case TypeKind::Array:
        case TypeKind::Vector:
        {
            auto result = nlohmann::json::array();
            const uint32_t count = Type()->Kind() == TypeKind::Array
                ? *Count() : Type()->GetIf<VectorTypeInfo>()->componentCount;
            for (uint32_t i = 0; i < count; ++i)
                result.push_back((Type()->Kind() == TypeKind::Array ? Element(i) : Component(i))->ToJSON());
            return result;
        }
        }
        return nullptr;
    }

    ValueViewResult<ValueView> ValueView::Parse(TypeInfoPtr rootType, std::span<const std::byte> data)
    {
        return parseImpl(std::move(rootType), data, {});
    }

    ValueViewResult<ValueView> ValueView::Parse(TypeInfoPtr rootType, TypeInfoDataPtr data)
    {
        if (!data)
            return std::unexpected(ValueViewError{ValueViewErrorCode::NullData, 0});

        std::shared_ptr<const TypeInfoData> ownedData(std::move(data));
        const std::span<const std::byte> bytes(*ownedData);
        return parseImpl(std::move(rootType), bytes, std::move(ownedData));
    }

    ValueViewResult<ValueView> ValueView::parseImpl(
        TypeInfoPtr rootType, std::span<const std::byte> data,
        std::shared_ptr<const TypeInfoData> ownedData)
    {
        if (!rootType)
            return std::unexpected(ValueViewError{ValueViewErrorCode::NullType, 0});
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
            return std::unexpected(ValueViewError{ValueViewErrorCode::DataTooLarge, 0});

        ParseContext context{
            .data = data,
            .ownedData = std::move(ownedData),
        };
        std::shared_ptr<State> state;
        auto end = parseValue(context, std::move(rootType), 0,
                              static_cast<uint32_t>(data.size()), 1, &state);
        if (!end)
            return std::unexpected(end.error());
        if (*end != data.size())
            return std::unexpected(ValueViewError{ValueViewErrorCode::RootSizeMismatch, *end});
        if (!state || state->offset != 0)
            return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, 0});

        return ValueView(std::move(state));
    }

    std::span<const std::byte> ValueView::Bytes() const noexcept
    {
        return state->data.subspan(state->offset, state->size);
    }

    ValueViewResult<ValueView> ValueView::Field(std::string_view name) const
    {
        const auto *structInfo = state->type->GetIf<StructTypeInfo>();
        if (!structInfo)
            return error(ValueViewErrorCode::WrongType);

        for (std::size_t i = 0; i < structInfo->fields.size(); ++i)
        {
            if (structInfo->fields[i].name == name)
                return state->children[i];
        }

        return error(ValueViewErrorCode::FieldNotFound);
    }

    ValueViewResult<ValueView> ValueView::Field(std::size_t index) const
    {
        if (state->type->Kind() != TypeKind::Struct)
            return error(ValueViewErrorCode::WrongType);
        if (index >= state->children.size())
            return error(ValueViewErrorCode::IndexOutOfRange);
        return state->children[index];
    }

    ValueViewResult<uint32_t> ValueView::Count() const noexcept
    {
        if (state->type->Kind() != TypeKind::Array)
            return error(ValueViewErrorCode::WrongType);
        return state->count;
    }

    ValueViewResult<ValueView> ValueView::Element(uint32_t index) const
    {
        const auto *arrayInfo = state->type->GetIf<ArrayTypeInfo>();
        if (!arrayInfo)
            return error(ValueViewErrorCode::WrongType);
        if (index >= state->count)
            return error(ValueViewErrorCode::IndexOutOfRange);

        if (!state->stride)
            return state->children[index];

        uint32_t relativeOffset;
        if (!checkedMultiply(index, *state->stride, relativeOffset))
            return error(ValueViewErrorCode::IntegerOverflow);
        uint32_t elementOffset;
        if (!checkedAdd(state->dataOffset, relativeOffset, elementOffset))
            return error(ValueViewErrorCode::IntegerOverflow);

        ParseContext context{
            .data = state->data,
            .ownedData = state->ownedData,
        };
        std::shared_ptr<State> elementState;
        uint32_t elementLimit;
        if (!checkedAdd(elementOffset, state->elementSize, elementLimit))
            return error(ValueViewErrorCode::IntegerOverflow);
        auto end = parseValue(context, arrayInfo->elementType, elementOffset,
                              elementLimit, 1, &elementState);
        if (!end)
            return std::unexpected(end.error());
        if (*end != elementLimit)
            return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, elementOffset});
        return ValueView(std::move(elementState));
    }

    ValueViewResult<ValueView> ValueView::Component(uint32_t index) const
    {
        const auto *vectorInfo = state->type->GetIf<VectorTypeInfo>();
        if (!vectorInfo)
            return error(ValueViewErrorCode::WrongType);
        if (index >= vectorInfo->componentCount)
            return error(ValueViewErrorCode::IndexOutOfRange);

        auto scalarByteSize = scalarSize(vectorInfo->scalarType->Kind());
        if (!scalarByteSize)
            return error(ValueViewErrorCode::InvalidType);

        uint32_t relativeOffset;
        if (!checkedMultiply(index, *scalarByteSize, relativeOffset))
            return error(ValueViewErrorCode::IntegerOverflow);
        uint32_t componentOffset;
        if (!checkedAdd(state->offset, relativeOffset, componentOffset))
            return error(ValueViewErrorCode::IntegerOverflow);

        auto componentState = std::make_shared<State>();
        componentState->type = vectorInfo->scalarType;
        componentState->ownedData = state->ownedData;
        componentState->data = state->data;
        componentState->offset = componentOffset;
        componentState->size = *scalarByteSize;
        return ValueView(std::move(componentState));
    }

    ValueViewResult<uint8_t> ValueView::AsByte() const
    {
        if (state->type->Kind() != TypeKind::Byte)
            return error(ValueViewErrorCode::WrongType);
        return std::to_integer<uint8_t>(state->data[state->offset]);
    }

    ValueViewResult<int32_t> ValueView::AsInt32() const
    {
        if (state->type->Kind() != TypeKind::Int32)
            return error(ValueViewErrorCode::WrongType);
        return std::bit_cast<int32_t>(loadLittle32(state->data, state->offset));
    }

    ValueViewResult<int64_t> ValueView::AsInt64() const
    {
        if (state->type->Kind() != TypeKind::Int64)
            return error(ValueViewErrorCode::WrongType);
        return std::bit_cast<int64_t>(loadLittle64(state->data, state->offset));
    }

    ValueViewResult<float> ValueView::AsFloat32() const
    {
        if (state->type->Kind() != TypeKind::Float32)
            return error(ValueViewErrorCode::WrongType);
        return std::bit_cast<float>(loadLittle32(state->data, state->offset));
    }

    ValueViewResult<double> ValueView::AsFloat64() const
    {
        if (state->type->Kind() != TypeKind::Float64)
            return error(ValueViewErrorCode::WrongType);
        return std::bit_cast<double>(loadLittle64(state->data, state->offset));
    }

    ValueViewResult<std::string_view> ValueView::AsString() const
    {
        if (state->type->Kind() != TypeKind::String)
            return error(ValueViewErrorCode::WrongType);

        const int32_t byteLength = std::bit_cast<int32_t>(loadLittle32(state->data, state->offset));
        if (byteLength < 0)
            return error(ValueViewErrorCode::NegativeLength);
        const char *bytes = reinterpret_cast<const char *>(
            state->data.data() + state->offset + sizeof(uint32_t));
        return std::string_view(bytes, static_cast<std::size_t>(byteLength));
    }

    ValueView::ValueView(std::shared_ptr<State> state) noexcept
        : state(std::move(state))
    {
    }

    std::unexpected<ValueViewError> ValueView::error(ValueViewErrorCode code) const noexcept
    {
        return std::unexpected(ValueViewError{code, state ? state->offset : 0});
    }

    ValueViewResult<uint32_t> ValueView::parseValue(
        ParseContext &context, TypeInfoPtr type, uint32_t cursor, uint32_t limit,
        uint32_t depth, std::shared_ptr<State> *output)
    {
        if (!type)
            return std::unexpected(ValueViewError{ValueViewErrorCode::NullType, cursor});
        if (depth > MaxDepth)
            return std::unexpected(ValueViewError{ValueViewErrorCode::DepthLimitExceeded, cursor});

        auto aligned = alignCursor(context.data, cursor, type->Alignment(), limit);
        if (!aligned)
            return std::unexpected(aligned.error());
        const uint32_t start = *aligned;
        cursor = start;

        std::shared_ptr<State> state;
        if (output)
        {
            if (context.materializedValues >= MaxMaterializedValues)
                return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, start});
            ++context.materializedValues;
            state = std::make_shared<State>();
            state->type = type;
            state->ownedData = context.ownedData;
            state->data = context.data;
            state->offset = start;
        }

        switch (type->Kind())
        {
        case TypeKind::Byte:
            if (!advance(cursor, 1, limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            break;
        case TypeKind::Int32:
        case TypeKind::Float32:
            if (!advance(cursor, 4, limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            break;
        case TypeKind::Int64:
        case TypeKind::Float64:
            if (!advance(cursor, 8, limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            break;
        case TypeKind::Vector:
        {
            const auto *vectorInfo = type->GetIf<VectorTypeInfo>();
            if (!vectorInfo || !vectorInfo->scalarType ||
                vectorInfo->componentCount < 2 || vectorInfo->componentCount > 4)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, start});
            auto scalarByteSize = scalarSize(vectorInfo->scalarType->Kind());
            if (!scalarByteSize)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, start});
            uint32_t size;
            if (!checkedMultiply(*scalarByteSize, vectorInfo->componentCount, size))
                return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, start});
            if (!advance(cursor, size, limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            break;
        }
        case TypeKind::String:
        {
            if (!rangeInBounds(cursor, sizeof(uint32_t), limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            const int32_t byteLength = std::bit_cast<int32_t>(loadLittle32(context.data, cursor));
            if (byteLength < 0)
                return std::unexpected(ValueViewError{ValueViewErrorCode::NegativeLength, cursor});
            cursor += sizeof(uint32_t);
            const uint32_t length = static_cast<uint32_t>(byteLength);
            if (length > MaxStringByteLength)
                return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, cursor});
            if (!rangeInBounds(cursor, length, limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            if (!isValidUtf8(context.data.subspan(cursor, length)))
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidUtf8, cursor});
            cursor += length;
            break;
        }
        case TypeKind::Array:
        {
            const auto *arrayInfo = type->GetIf<ArrayTypeInfo>();
            if (!arrayInfo || !arrayInfo->elementType)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, start});
            if (!rangeInBounds(cursor, sizeof(uint32_t), limit))
                return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
            const int32_t signedCount = std::bit_cast<int32_t>(loadLittle32(context.data, cursor));
            if (signedCount < 0)
                return std::unexpected(ValueViewError{ValueViewErrorCode::NegativeLength, cursor});
            const uint32_t count = static_cast<uint32_t>(signedCount);
            const uint32_t maxCount = arrayInfo->elementType->Kind() == TypeKind::Byte
                                          ? MaxByteArrayElementCount
                                          : MaxArrayElementCount;
            if (count > maxCount)
                return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, cursor});

            cursor += sizeof(uint32_t);
            auto dataOffset = alignCursor(context.data, cursor,
                                          arrayInfo->elementType->Alignment(), limit);
            if (!dataOffset)
                return std::unexpected(dataOffset.error());
            cursor = *dataOffset;

            auto elementSize = fixedSize(arrayInfo->elementType, depth + 1);
            if (!elementSize)
                return std::unexpected(elementSize.error());

            if (state)
            {
                state->count = count;
                state->dataOffset = cursor;
            }

            if (*elementSize)
            {
                uint32_t stride;
                if (!checkedAlignUp(**elementSize, arrayInfo->elementType->Alignment(), stride))
                    return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, cursor});
                if (state)
                {
                    state->elementSize = **elementSize;
                    state->stride = stride;
                }

                if (count != 0)
                {
                    uint32_t lastOffset;
                    uint32_t precedingSize;
                    if (!checkedMultiply(count - 1, stride, precedingSize) ||
                        !checkedAdd(cursor, precedingSize, lastOffset))
                        return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, cursor});
                    uint32_t end;
                    if (!checkedAdd(lastOffset, **elementSize, end))
                        return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, lastOffset});
                    if (end > limit)
                        return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, lastOffset});

                    if (needsPaddingValidation(arrayInfo->elementType, depth + 1))
                    {
                        uint32_t elementCursor = cursor;
                        for (uint32_t i = 0; i < count; ++i)
                        {
                            auto parsed = parseValue(context, arrayInfo->elementType, elementCursor,
                                                     end, depth + 1, nullptr);
                            if (!parsed)
                                return std::unexpected(parsed.error());
                            elementCursor = *parsed;
                        }
                    }
                    cursor = end;
                }
            }
            else
            {
                if (state)
                {
                    if (count > MaxMaterializedValues - context.materializedValues)
                        return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, cursor});
                    state->children.reserve(count);
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    std::shared_ptr<State> child;
                    auto parsed = parseValue(context, arrayInfo->elementType, cursor,
                                             limit, depth + 1, state ? &child : nullptr);
                    if (!parsed)
                        return std::unexpected(parsed.error());
                    cursor = *parsed;
                    if (state)
                        state->children.emplace_back(ValueView(std::move(child)));
                }
            }
            break;
        }
        case TypeKind::Struct:
        {
            const auto *structInfo = type->GetIf<StructTypeInfo>();
            if (!structInfo)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, start});
            if (structInfo->fields.size() > MaxStructFieldCount)
                return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, start});
            if (state)
            {
                if (structInfo->fields.size() > MaxMaterializedValues - context.materializedValues)
                    return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, start});
                state->children.reserve(structInfo->fields.size());
            }
            for (const auto &field : structInfo->fields)
            {
                if (!field.type)
                    return std::unexpected(ValueViewError{ValueViewErrorCode::NullType, cursor});
                std::shared_ptr<State> child;
                auto parsed = parseValue(context, field.type, cursor, limit,
                                         depth + 1, state ? &child : nullptr);
                if (!parsed)
                    return std::unexpected(parsed.error());
                cursor = *parsed;
                if (state)
                    state->children.emplace_back(ValueView(std::move(child)));
            }

            auto end = alignCursor(context.data, cursor, type->Alignment(), limit);
            if (!end)
                return std::unexpected(end.error());
            cursor = *end;
            break;
        }
        }

        if (state)
        {
            state->size = cursor - start;
            *output = std::move(state);
        }
        return cursor;
    }

    ValueViewResult<std::optional<uint32_t>> ValueView::fixedSize(
        const TypeInfoPtr &type, uint32_t depth)
    {
        if (!type)
            return std::unexpected(ValueViewError{ValueViewErrorCode::NullType, 0});
        if (depth > MaxDepth)
            return std::unexpected(ValueViewError{ValueViewErrorCode::DepthLimitExceeded, 0});

        switch (type->Kind())
        {
        case TypeKind::Byte:
            return std::optional<uint32_t>(1);
        case TypeKind::Int32:
        case TypeKind::Float32:
            return std::optional<uint32_t>(4);
        case TypeKind::Int64:
        case TypeKind::Float64:
            return std::optional<uint32_t>(8);
        case TypeKind::String:
        case TypeKind::Array:
            return std::optional<uint32_t>();
        case TypeKind::Vector:
        {
            const auto *info = type->GetIf<VectorTypeInfo>();
            if (!info || !info->scalarType)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, 0});
            auto scalarByteSize = scalarSize(info->scalarType->Kind());
            if (!scalarByteSize || info->componentCount < 2 || info->componentCount > 4)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, 0});
            uint32_t size;
            if (!checkedMultiply(*scalarByteSize, info->componentCount, size))
                return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, 0});
            return std::optional<uint32_t>(size);
        }
        case TypeKind::Struct:
        {
            const auto *info = type->GetIf<StructTypeInfo>();
            if (!info)
                return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, 0});
            uint32_t cursor = 0;
            for (const auto &field : info->fields)
            {
                auto fieldSize = fixedSize(field.type, depth + 1);
                if (!fieldSize)
                    return std::unexpected(fieldSize.error());
                if (!*fieldSize)
                    return std::optional<uint32_t>();
                if (!checkedAlignUp(cursor, field.type->Alignment(), cursor) ||
                    !checkedAdd(cursor, **fieldSize, cursor))
                    return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, 0});
            }
            if (!checkedAlignUp(cursor, type->Alignment(), cursor))
                return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, 0});
            return std::optional<uint32_t>(cursor);
        }
        }

        return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidType, 0});
    }

    bool ValueView::needsPaddingValidation(const TypeInfoPtr &type, uint32_t depth)
    {
        if (!type || depth > MaxDepth || type->Kind() != TypeKind::Struct)
            return false;
        const auto *info = type->GetIf<StructTypeInfo>();
        if (!info)
            return false;

        uint32_t cursor = 0;
        for (const auto &field : info->fields)
        {
            uint32_t aligned;
            if (!checkedAlignUp(cursor, field.type->Alignment(), aligned))
                return true;
            if (aligned != cursor || needsPaddingValidation(field.type, depth + 1))
                return true;
            auto size = fixedSize(field.type, depth + 1);
            if (!size || !*size || !checkedAdd(aligned, **size, cursor))
                return true;
        }
        uint32_t aligned;
        return !checkedAlignUp(cursor, type->Alignment(), aligned) || aligned != cursor;
    }

    ValueViewResult<uint32_t> ValueView::alignCursor(
        std::span<const std::byte> data, uint32_t cursor, uint32_t alignment, uint32_t limit)
    {
        uint32_t aligned;
        if (!checkedAlignUp(cursor, alignment, aligned))
            return std::unexpected(ValueViewError{ValueViewErrorCode::IntegerOverflow, cursor});
        if (aligned > limit || aligned > data.size())
            return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, cursor});
        for (uint32_t i = cursor; i < aligned; ++i)
        {
            if (data[i] != std::byte{0})
                return std::unexpected(ValueViewError{ValueViewErrorCode::NonZeroPadding, i});
        }
        return aligned;
    }

    bool ValueView::advance(uint32_t &cursor, uint32_t size, uint32_t limit) noexcept
    {
        if (!rangeInBounds(cursor, size, limit))
            return false;
        cursor += size;
        return true;
    }

    bool ValueView::rangeInBounds(uint32_t offset, uint32_t size, uint32_t limit) noexcept
    {
        return offset <= limit && size <= limit - offset;
    }

    bool ValueView::checkedAdd(uint32_t lhs, uint32_t rhs, uint32_t &result) noexcept
    {
        if (rhs > std::numeric_limits<uint32_t>::max() - lhs)
            return false;
        result = lhs + rhs;
        return result <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    }

    bool ValueView::checkedMultiply(uint32_t lhs, uint32_t rhs, uint32_t &result) noexcept
    {
        if (lhs != 0 && rhs > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) / lhs)
            return false;
        result = lhs * rhs;
        return true;
    }

    bool ValueView::checkedAlignUp(uint32_t value, uint32_t alignment, uint32_t &result) noexcept
    {
        if (alignment == 0)
            return false;
        const uint32_t remainder = value % alignment;
        if (remainder == 0)
        {
            result = value;
            return value <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
        }
        return checkedAdd(value, alignment - remainder, result);
    }

    std::optional<uint32_t> ValueView::scalarSize(TypeKind kind) noexcept
    {
        switch (kind)
        {
        case TypeKind::Int32:
        case TypeKind::Float32:
            return 4;
        case TypeKind::Int64:
        case TypeKind::Float64:
            return 8;
        default:
            return std::nullopt;
        }
    }

    uint32_t ValueView::loadLittle32(std::span<const std::byte> data, uint32_t offset) noexcept
    {
        uint32_t value = 0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return value;
    }

    uint64_t ValueView::loadLittle64(std::span<const std::byte> data, uint32_t offset) noexcept
    {
        uint64_t value = 0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        if constexpr (std::endian::native == std::endian::big)
            value = std::byteswap(value);
        return value;
    }

    bool ValueView::isValidUtf8(std::span<const std::byte> bytes) noexcept
    {
        std::size_t i = 0;
        while (i < bytes.size())
        {
            const uint8_t first = std::to_integer<uint8_t>(bytes[i]);
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
            const uint8_t second = std::to_integer<uint8_t>(bytes[i + 1]);
            if (second < secondMin || second > secondMax)
                return false;
            for (std::size_t j = 2; j < length; ++j)
            {
                const uint8_t continuation = std::to_integer<uint8_t>(bytes[i + j]);
                if (continuation < 0x80 || continuation > 0xbf)
                    return false;
            }
            i += length;
        }
        return true;
    }
}
