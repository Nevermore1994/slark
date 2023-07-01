//
//  DecoderManager.hpp
//  slark
//
//  Created by Nevermore on 2022/4/24.
//

#include "IDecoder.h"
#include "NonCopyable.h"
#include <unordered_map>
#include <memory>

namespace slark {

class DecoderManager : public NonCopyable {

public:
    inline static DecoderManager& shareInstance() {
        static DecoderManager instance;
        return instance;
    }

    DecoderManager() = default;

    ~DecoderManager() override = default;

public:
    void init() noexcept;

    bool contains(DecoderType coderType) noexcept;

    std::unique_ptr<IDecoder> create(DecoderType type) noexcept;

private:
    std::unordered_map<std::string_view, DecoderInfo> decoders_;
};

}
