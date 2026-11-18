//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "MediaPlaylist.h"
#include "NonCopyable.h"

namespace slark {

class M3U8Parser : public NonCopyMovable {
 public:
    explicit M3U8Parser(std::string baseUrl);
    ~M3U8Parser() = default;

    void reset() noexcept;
    bool parse(std::string_view content) noexcept;

    [[nodiscard]] static std::string resolveUrl(std::string_view baseUrl,
                                                std::string_view relativeUrl) noexcept;

    [[nodiscard]] std::shared_ptr<MediaPlaylist> playlist() const noexcept {
        return playlist_;
    }

    [[nodiscard]] bool isMasterPlaylist() const noexcept {
        return isMasterPlaylist_;
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        return isCompleted_;
    }

 private:
    bool parseMasterPlaylist(std::string_view content) noexcept;
    bool parseMediaPlaylist(std::string_view content) noexcept;

    bool isMasterPlaylist_ = false;
    bool isCompleted_ = false;
    std::string baseUrl_;
    std::shared_ptr<MediaPlaylist> playlist_;
};

}  // namespace slark
