//
//  DecoderManager.hpp
//  slark
//
//  Created by Nevermore on 2022/4/24.
//

#include "IDecoder.h"
#include "NonCopyable.h"
#include <string_view>
#include <unordered_map>
#include <memory>

namespace slark {

class DecoderManager : public NonCopyable {

public:
    static DecoderManager& shareInstance();

    DecoderManager();
    ~DecoderManager() override = default;

public:
    void init() noexcept;
    DecoderType getDecoderType(std::string_view mediaInfo, bool isSoftDecode) const;
    bool contains(DecoderType coderType) const noexcept;
    std::unique_ptr<IDecoder> create(DecoderType type) const noexcept;

private:
    std::unordered_map<std::string, DecoderType> mediaInfo_;
    std::unordered_map<DecoderType, DecoderInfo> decoderInfo_;
};

}
