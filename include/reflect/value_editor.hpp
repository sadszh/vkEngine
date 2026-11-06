#ifndef VKE_REFLECT_VALUE_EDITOR_H
#define VKE_REFLECT_VALUE_EDITOR_H

#include <reflect/value_view.hpp>

#include <array>
#include <bit>
#include <cmath>

namespace vke_common
{
    // Edits a borrowed binary buffer. The buffer must outlive this editor and may
    // only be modified through it. Reacquire all views from Root() after a string
    // or array edit; those operations can replace the allocation and layout.
    class ValueEditor final
    {
    public:
        static ValueViewResult<ValueEditor> Parse(TypeInfoPtr type, TypeInfoData &data);
        const ValueView &Root() const noexcept { return root; }

        template <typename T>
        ValueViewResult<void> SetScalar(const ValueView &value, T scalar)
        {
            constexpr TypeKind kind = [] {
                if constexpr (std::is_same_v<T, uint8_t>) return TypeKind::Byte;
                else if constexpr (std::is_same_v<T, int32_t>) return TypeKind::Int32;
                else if constexpr (std::is_same_v<T, int64_t>) return TypeKind::Int64;
                else if constexpr (std::is_same_v<T, float>) return TypeKind::Float32;
                else if constexpr (std::is_same_v<T, double>) return TypeKind::Float64;
                else static_assert(!sizeof(T), "unsupported binary scalar");
            }();
            if (auto valid = check(value, kind); !valid) return valid;
            if constexpr (std::is_floating_point_v<T>)
                if (!std::isfinite(scalar))
                    return std::unexpected(ValueViewError{ValueViewErrorCode::InvalidNumber, value.Offset()});
            const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(scalar);
            for (size_t i = 0; i < bytes.size(); ++i)
                (*data)[value.Offset() + i] = bytes[std::endian::native == std::endian::little ? i : bytes.size() - 1 - i];
            return {};
        }

        ValueViewResult<void> SetString(const ValueView &value, std::string_view text);
        ValueViewResult<void> AppendElement(const ValueView &array);
        ValueViewResult<void> RemoveElement(const ValueView &array, uint32_t index);

    private:
        TypeInfoData *data;
        ValueView root;
        ValueEditor(TypeInfoData &data, ValueView root) : data(&data), root(std::move(root)) {}
        ValueViewResult<void> check(const ValueView &value, TypeKind kind) const;
        ValueViewResult<void> replace(const ValueView &value, const TypeInfoData &replacement);
    };
}

#endif
