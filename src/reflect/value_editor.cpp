#include <reflect/value_editor.hpp>

#include <algorithm>

namespace vke_common
{
    namespace
    {
        // All TypeInfo alignments include their children's alignments, so an
        // unchanged subtree can be copied verbatim at its new aligned offset.
        struct Writer
        {
            TypeInfoData bytes;
            bool overflow = false;

            void zeros(size_t count)
            {
                if (count > static_cast<size_t>(INT32_MAX) - bytes.size()) overflow = true;
                if (!overflow) bytes.resize(bytes.size() + count, std::byte{0});
            }
            void align(uint32_t alignment)
            {
                zeros((alignment - bytes.size() % alignment) % alignment);
            }
            void append(std::span<const std::byte> source)
            {
                if (source.size() > static_cast<size_t>(INT32_MAX) - bytes.size()) overflow = true;
                if (!overflow) bytes.insert(bytes.end(), source.begin(), source.end());
            }
            void count(uint32_t value)
            {
                for (size_t i = 0; i < 4; ++i)
                    bytes[i] = static_cast<std::byte>((value >> (8 * i)) & 0xff);
            }
            void defaultValue(const TypeInfoPtr &type, uint32_t depth = 1)
            {
                if (depth > ValueView::MaxDepth) { overflow = true; return; }
                align(type->Alignment());
                switch (type->Kind())
                {
                case TypeKind::Byte: zeros(1); break;
                case TypeKind::Int32:
                case TypeKind::Float32:
                case TypeKind::String: zeros(4); break;
                case TypeKind::Int64:
                case TypeKind::Float64: zeros(8); break;
                case TypeKind::Vector:
                {
                    const auto &vector = *type->GetIf<VectorTypeInfo>();
                    for (uint32_t i = 0; i < vector.componentCount; ++i)
                        defaultValue(vector.scalarType, depth + 1);
                    break;
                }
                case TypeKind::Array:
                    zeros(4);
                    align(type->GetIf<ArrayTypeInfo>()->elementType->Alignment());
                    break;
                case TypeKind::Struct:
                    for (const auto &field : type->GetIf<StructTypeInfo>()->fields)
                        defaultValue(field.type, depth + 1);
                    align(type->Alignment());
                    break;
                }
            }

            void rewrite(const ValueView &value, const ValueView &target,
                         const TypeInfoData &replacement)
            {
                if (overflow) return;
                align(value.Type()->Alignment());
                if (value.Offset() == target.Offset() && value.Type() == target.Type())
                    append(replacement);
                else if (target.Offset() < value.Offset() ||
                         target.Offset() >= value.Offset() + value.Size())
                    append(value.Bytes());
                else if (const auto *structure = value.Type()->GetIf<StructTypeInfo>())
                {
                    for (std::size_t i = 0; i < structure->fields.size(); ++i)
                        rewrite(*value.Field(i), target, replacement);
                    align(value.Type()->Alignment());
                }
                else if (const auto *array = value.Type()->GetIf<ArrayTypeInfo>())
                {
                    append(value.Bytes().first(4));
                    align(array->elementType->Alignment());
                    for (uint32_t i = 0; i < *value.Count(); ++i)
                        rewrite(*value.Element(i), target, replacement);
                }
                else
                    append(value.Bytes());
            }
        };
    }

    ValueViewResult<ValueEditor> ValueEditor::Parse(TypeInfoPtr type, TypeInfoData &data)
    {
        auto root = ValueView::Parse(std::move(type), data);
        if (!root) return std::unexpected(root.error());
        return ValueEditor(data, std::move(*root));
    }

    ValueViewResult<void> ValueEditor::check(const ValueView &value, TypeKind kind) const
    {
        if (!value.Valid() || value.Type()->Kind() != kind)
            return std::unexpected(ValueViewError{ValueViewErrorCode::WrongType, 0});
        if (value.state->data.data() != data->data() || value.state->data.size() != data->size())
            return std::unexpected(ValueViewError{ValueViewErrorCode::OutOfBounds, value.Offset()});
        return {};
    }

    ValueViewResult<void> ValueEditor::replace(const ValueView &value, const TypeInfoData &replacement)
    {
        Writer writer;
        writer.rewrite(root, value, replacement);
        if (writer.overflow)
            return std::unexpected(ValueViewError{ValueViewErrorCode::DataTooLarge, value.Offset()});
        // Validate the complete layout before committing. Failure leaves both
        // the original bytes and the current views intact.
        auto next = ValueView::Parse(root.Type(), writer.bytes);
        if (!next) return std::unexpected(next.error());
        data->swap(writer.bytes);
        root = std::move(*next);
        return {};
    }

    ValueViewResult<void> ValueEditor::SetString(const ValueView &value, std::string_view text)
    {
        if (auto valid = check(value, TypeKind::String); !valid) return valid;
        if (text.size() > ValueView::MaxStringByteLength)
            return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, value.Offset()});
        Writer writer;
        writer.zeros(4);
        writer.count(static_cast<uint32_t>(text.size()));
        writer.append(std::as_bytes(std::span(text.data(), text.size())));
        if (writer.bytes.size() == value.Size())
        {
            auto validated = ValueView::Parse(value.Type(), writer.bytes);
            if (!validated) return std::unexpected(validated.error());
            std::copy(writer.bytes.begin(), writer.bytes.end(), data->begin() + value.Offset());
            return {};
        }
        return replace(value, writer.bytes);
    }

    ValueViewResult<void> ValueEditor::AppendElement(const ValueView &array)
    {
        if (auto valid = check(array, TypeKind::Array); !valid) return valid;
        const auto &element = array.Type()->GetIf<ArrayTypeInfo>()->elementType;
        const auto count = *array.Count();
        const auto maximum = element->Kind() == TypeKind::Byte
            ? ValueView::MaxByteArrayElementCount : ValueView::MaxArrayElementCount;
        if (count >= maximum)
            return std::unexpected(ValueViewError{ValueViewErrorCode::ValueLimitExceeded, array.Offset()});
        Writer writer;
        writer.append(array.Bytes());
        writer.count(count + 1);
        writer.defaultValue(element);
        if (writer.overflow)
            return std::unexpected(ValueViewError{ValueViewErrorCode::DataTooLarge, array.Offset()});
        return replace(array, writer.bytes);
    }

    ValueViewResult<void> ValueEditor::RemoveElement(const ValueView &array, uint32_t index)
    {
        if (auto valid = check(array, TypeKind::Array); !valid) return valid;
        const auto count = *array.Count();
        if (index >= count)
            return std::unexpected(ValueViewError{ValueViewErrorCode::IndexOutOfRange, array.Offset()});
        const auto alignment = array.Type()->GetIf<ArrayTypeInfo>()->elementType->Alignment();
        Writer writer;
        if (index == 0)
        {
            writer.zeros(4);
            writer.align(alignment);
        }
        else
        {
            const auto previous = array.Element(index - 1);
            writer.append(array.Bytes().first(previous->Offset() + previous->Size() - array.Offset()));
        }
        writer.count(count - 1);
        if (index + 1 < count)
        {
            writer.align(alignment);
            writer.append(array.Bytes().subspan(array.Element(index + 1)->Offset() - array.Offset()));
        }
        return replace(array, writer.bytes);
    }
}
