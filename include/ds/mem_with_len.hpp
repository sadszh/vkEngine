#ifndef MEM_WITH_LEN_H
#define MEM_WITH_LEN_H

#include <cstddef>
#include <binary_io.hpp>
#include <logger.hpp>

namespace vke_ds
{

    template <size_t Alignment>
    class Memory
    {
        static_assert((Alignment & (Alignment - 1)) == 0,
                      "Alignment must be power of two");

    public:
        std::byte *data = nullptr;
        size_t size = 0;

        Memory() = default;

        explicit Memory(size_t size) noexcept : size(size)
        {
            if (size > 0)
                data = static_cast<std::byte *>(
                    ::operator new(size, std::align_val_t(Alignment)));
        }

        Memory &operator=(const Memory &) = delete;
        Memory(const Memory &) = delete;

        Memory &operator=(Memory &&ano) noexcept
        {
            if (this != &ano)
            {
                if (data)
                    ::operator delete(data, std::align_val_t(Alignment));
                data = ano.data;
                size = ano.size;
                ano.data = nullptr;
                ano.size = 0;
            }
            return *this;
        }

        Memory(Memory &&ano) noexcept : data(ano.data), size(ano.size)
        {
            ano.data = nullptr;
            ano.size = 0;
        }

        void WriteToBinaryStream(std::ostream &binary) const
        {
            VKE_LOG_INFO("WriteToBinaryStream {}", size);
            if (size > 0 && data)
            {
                vke_common::WriteBytes(binary, &size, sizeof(size_t));
                vke_common::WriteBytes(binary, data, size);
            }
        }

        ~Memory() noexcept
        {
            if (data != nullptr)
                ::operator delete(data, std::align_val_t(Alignment));
        }
    };
}

#endif