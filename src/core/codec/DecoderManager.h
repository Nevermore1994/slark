//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <memory>
#include <unordered_map>

#include "IDecoder.h"
#include "NonCopyable.h"

namespace slark {

class DecoderManager : public NonCopyMovable {
 public:
    static DecoderManager& shareInstance() noexcept;

    DecoderManager() noexcept;
    ~DecoderManager() = default;

 public:
    void init() noexcept;
    DecoderType getDecoderType(std::string_view mediaInfo, bool isSoftDecode) const noexcept;
    bool contains(DecoderType coderType) const noexcept;
    std::shared_ptr<IDecoder> create(DecoderType type) const noexcept;

 private:
    std::unordered_map<std::string, DecoderType> mediaInfo_;
    std::unordered_map<DecoderType, DecoderTypeInfo> decoderInfo_;
};

}  // namespace slark
