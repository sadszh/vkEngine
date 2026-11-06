#ifndef VKE_REFLECT_VALUE_VIEW_H
#define VKE_REFLECT_VALUE_VIEW_H

#include <reflect/type_info.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace vke_common
{
    enum class ValueViewErrorCode
    {
        NullType,
        DataTooLarge,
        IntegerOverflow,
        OutOfBounds,
        NonZeroPadding,
        NullData,
        NegativeLength,
        InvalidUtf8,
        InvalidType,
        DepthLimitExceeded,
        ValueLimitExceeded,
        RootSizeMismatch,
        WrongType,
        FieldNotFound,
        IndexOutOfRange,
        InvalidNumber,
    };

    struct ValueViewError
    {
        ValueViewErrorCode code;
        uint32_t offset;
    };

    template <typename T>
    using ValueViewResult = Result<T, ValueViewError>;

    constexpr std::string_view ToString(ValueViewErrorCode code) noexcept
    {
        switch (code)
        {
        case ValueViewErrorCode::NullType:
            return "null type";
        case ValueViewErrorCode::DataTooLarge:
            return "data is too large";
        case ValueViewErrorCode::IntegerOverflow:
            return "integer overflow";
        case ValueViewErrorCode::OutOfBounds:
            return "value is out of data bounds";
        case ValueViewErrorCode::NonZeroPadding:
            return "padding contains a non-zero byte";
        case ValueViewErrorCode::NullData:
            return "null owned data";
        case ValueViewErrorCode::NegativeLength:
            return "length or count is negative";
        case ValueViewErrorCode::InvalidUtf8:
            return "string is not valid UTF-8";
        case ValueViewErrorCode::InvalidType:
            return "type information is invalid";
        case ValueViewErrorCode::DepthLimitExceeded:
            return "value nesting depth limit exceeded";
        case ValueViewErrorCode::ValueLimitExceeded:
            return "materialized value limit exceeded";
        case ValueViewErrorCode::RootSizeMismatch:
            return "root value does not consume the complete data";
        case ValueViewErrorCode::WrongType:
            return "value has the wrong type";
        case ValueViewErrorCode::FieldNotFound:
            return "struct field was not found";
        case ValueViewErrorCode::IndexOutOfRange:
            return "value index is out of range";
        case ValueViewErrorCode::InvalidNumber:
            return "number is not finite";
        }

        return "unknown ValueView error";
    }

    class ValueView final
    {
    public:
        static constexpr uint32_t MaxDepth = 64;
        static constexpr uint32_t MaxMaterializedValues = 1'048'576;
        static constexpr uint32_t MaxStructFieldCount = 4'096;
        static constexpr uint32_t MaxStringByteLength = 256u * 1024u * 1024u;
        static constexpr uint32_t MaxArrayElementCount = 16'777'216;
        static constexpr uint32_t MaxByteArrayElementCount = 1'073'741'824;

        // The returned view does not own data. The memory must remain valid and
        // at a stable address for the complete lifetime of every copied child view.
        static ValueViewResult<ValueView> Parse(TypeInfoPtr rootType, std::span<const std::byte> data);

        // Takes ownership of data. Every copied child view keeps the allocation alive.
        static ValueViewResult<ValueView> Parse(TypeInfoPtr rootType, TypeInfoDataPtr data);

        bool Valid() const noexcept { return static_cast<bool>(state); }
        const TypeInfoPtr &Type() const noexcept { return state->type; }
        uint32_t Offset() const noexcept { return state->offset; }
        uint32_t Size() const noexcept { return state->size; }
        std::span<const std::byte> Bytes() const noexcept;

        ValueViewResult<ValueView> Field(std::string_view name) const;
        ValueViewResult<ValueView> Field(std::size_t index) const;
        ValueViewResult<uint32_t> Count() const noexcept;
        ValueViewResult<ValueView> Element(uint32_t index) const;
        ValueViewResult<ValueView> Component(uint32_t index) const;

        ValueViewResult<uint8_t> AsByte() const;
        ValueViewResult<int32_t> AsInt32() const;
        ValueViewResult<int64_t> AsInt64() const;
        ValueViewResult<float> AsFloat32() const;
        ValueViewResult<double> AsFloat64() const;
        ValueViewResult<std::string_view> AsString() const;

        nlohmann::json ToJSON() const;

    private:
        friend class ValueEditor;
        struct State
        {
            TypeInfoPtr type;
            std::shared_ptr<const TypeInfoData> ownedData;
            std::span<const std::byte> data;
            uint32_t offset = 0;
            uint32_t size = 0;

            uint32_t count = 0;
            uint32_t dataOffset = 0;
            uint32_t elementSize = 0;
            std::optional<uint32_t> stride;

            std::vector<ValueView> children;
        };

        struct ParseContext
        {
            std::span<const std::byte> data;
            std::shared_ptr<const TypeInfoData> ownedData;
            uint32_t materializedValues = 0;
        };

        std::shared_ptr<const State> state;

        explicit ValueView(std::shared_ptr<State> state) noexcept;
        std::unexpected<ValueViewError> error(ValueViewErrorCode code) const noexcept;

        static ValueViewResult<ValueView> parseImpl(TypeInfoPtr rootType, std::span<const std::byte> data,
                                std::shared_ptr<const TypeInfoData> ownedData);

        static ValueViewResult<uint32_t> parseValue(
            ParseContext &context, TypeInfoPtr type, uint32_t cursor, uint32_t limit,
            uint32_t depth, std::shared_ptr<State> *output);
        static ValueViewResult<std::optional<uint32_t>> fixedSize(
            const TypeInfoPtr &type, uint32_t depth);
        static bool needsPaddingValidation(const TypeInfoPtr &type, uint32_t depth);
        static ValueViewResult<uint32_t> alignCursor(
            std::span<const std::byte> data, uint32_t cursor, uint32_t alignment, uint32_t limit);
        static bool advance(uint32_t &cursor, uint32_t size, uint32_t limit) noexcept;
        static bool rangeInBounds(uint32_t offset, uint32_t size, uint32_t limit) noexcept;
        static bool checkedAdd(uint32_t lhs, uint32_t rhs, uint32_t &result) noexcept;
        static bool checkedMultiply(uint32_t lhs, uint32_t rhs, uint32_t &result) noexcept;
        static bool checkedAlignUp(uint32_t value, uint32_t alignment, uint32_t &result) noexcept;
        static std::optional<uint32_t> scalarSize(TypeKind kind) noexcept;
        static uint32_t loadLittle32(std::span<const std::byte> data, uint32_t offset) noexcept;
        static uint64_t loadLittle64(std::span<const std::byte> data, uint32_t offset) noexcept;
        static bool isValidUtf8(std::span<const std::byte> bytes) noexcept;
    };
}

#endif
