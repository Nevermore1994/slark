//
//  DecoderManager.hpp
//  slark
//
//  Created by Nevermore on 2022/4/24.
//

#include <unordered_map>
#include <memory>
#include "IDecoder.h"
#include "NonCopyable.h"

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
    std::shared_ptr<IDecoder> create(DecoderType type) const noexcept;

private:
    std::unordered_map<std::string, DecoderType> mediaInfo_;
    std::unordered_map<DecoderType, DecoderTypeInfo> decoderInfo_;
};

}
