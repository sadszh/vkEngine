#ifndef BINARY_IO_H
#define BINARY_IO_H

#include <cstddef>
#include <istream>
#include <ostream>

namespace vke_common
{
    inline bool ReadBytes(std::istream &input, void *data, std::size_t size)
    {
        input.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
        return !input.fail();
    }

    inline bool WriteBytes(std::ostream &output, const void *data, std::size_t size)
    {
        output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        return !output.fail();
    }
}

#endif
