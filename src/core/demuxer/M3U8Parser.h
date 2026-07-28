//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>
#include <string>

#include "MediaPlaylist.h"
#include "NonCopyable.h"

namespace slark {

class M3U8Parser : public NonCopyMovable {
 public:
    explicit M3U8Parser(std::string baseUrl);
    ~M3U8Parser() = default;

    void reset() noexcept;
    bool parse(const std::string& content) noexcept;

    [[nodiscard]] static std::string resolveUrl(const std::string& baseUrl,
                                                const std::string& relativeUrl) noexcept;

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
    bool parseMasterPlaylist(const std::string& content) noexcept;
    bool parseMediaPlaylist(const std::string& content) noexcept;

    bool isMasterPlaylist_ = false;
    bool isCompleted_ = false;
    std::string baseUrl_;
    std::shared_ptr<MediaPlaylist> playlist_;
};

}  // namespace slark
