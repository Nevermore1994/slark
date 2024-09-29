//
//  Buffer.hpp
//  slark
//
//  Created by Nevermore.
//

#include <expected>
#include <shared_mutex>
#include "Data.hpp"
#include "NonCopyable.h"

namespace slark {

class Buffer: public NonCopyable {
public:
    bool empty() const noexcept;

    bool checkMinSize(uint64_t) const noexcept;

    uint64_t length() const noexcept;
    
    void append(DataPtr) noexcept;
    
    std::expected<uint32_t, bool> read4ByteLE() noexcept;

    std::expected<uint32_t, bool> read3ByteLE() noexcept;

    std::expected<uint16_t, bool> read2ByteLE() noexcept;

    std::expected<uint8_t, bool> readByteLE() noexcept;

    std::expected<uint32_t, bool> read4ByteBE() noexcept;

    std::expected<uint32_t, bool> read3ByteBE() noexcept;

    std::expected<uint16_t, bool> read2ByteBE() noexcept;

    std::expected<uint8_t, bool> readByteBE() noexcept;
    
    DataPtr readData(uint64_t) noexcept;
    
    [[nodiscard]] uint64_t readPos() const noexcept {
        return readPos_;
    }
    
    void shrink() noexcept;
private:
    DataPtr data_ = nullptr;
    uint64_t readPos_ = 0;
};

}
