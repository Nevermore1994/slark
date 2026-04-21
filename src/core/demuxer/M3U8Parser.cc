//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"

#include "Log.hpp"

#include <charconv>
#include <utility>

namespace slark {

M3U8Parser::M3U8Parser(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {}

void M3U8Parser::reset() noexcept {
    isPlayList_ = false;
    isCompleted_ = false;
    index_ = 0;
    totalDuration_ = 0.0;
    infos_.clear();
    playListInfos_.clear();
}

namespace {

std::string spliceUrl(const std::string& baseUrl, std::string_view view) {
    if (view.starts_with("http")) {
        return std::string(view);
    }
    auto normalizeRelativePath = [](std::string base, std::string_view& v) {
        if (base.back() == '/') {
            base.pop_back();
        }
        while (v.starts_with("../")) {
            v.remove_prefix(3);
            auto pos = base.rfind('/');
            if (pos != std::string::npos) {
                base = base.substr(0, pos);
            } else {
                base.clear();
                break;
            }
        }
        return base;
    };

    if (view.starts_with("../")) {
        auto url = normalizeRelativePath(baseUrl, view);
        return url + '/' + std::string(view);
    }
    if (view.starts_with('/')) {
        auto protocolEnd = baseUrl.find("://");
        if (protocolEnd == std::string::npos) {
            return std::string(view);
        }
        protocolEnd += 2;
        auto hostEnd = baseUrl.find('/', protocolEnd);
        return baseUrl.substr(0, hostEnd) + std::string(view);
    }
    while (view.starts_with("./")) {
        view.remove_prefix(2);
    }
    return baseUrl + "/" + std::string(view);
}

} // namespace

bool M3U8Parser::parse(Buffer& buffer) noexcept {
    DataView line;
    while (buffer.readLine(line)) {
        auto content = line.removeSpace();
        if (content.empty()) {
            continue;
        }
        auto view = std::string_view(content);
        if (view.starts_with("#EXTINF:")) {
            view = view.substr(8);
            view = view.substr(0, view.find(','));
            TSInfo info;
            info.sequence = index_++;
            // 使用 from_chars 避免 strtod 需要 null-terminated 字符串的 UB 风险
            double dur = 0.0;
            auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), dur);
            if (ec != std::errc{}) {
                LogE("invalid EXTINF duration: {}", view);
                dur = 0.0;
            }
            info.duration = dur;
            info.startTime = totalDuration_;
            infos_.push_back(info);
            totalDuration_ += info.duration;
        } else if (view.starts_with("#EXT-X-BYTERANGE:")) {
            view = view.substr(17);
            if (infos_.empty()) {
                LogE("parse byte range error: info array is empty!");
                break;
            }
            auto& info = infos_.back();
            info.isSupportByteRange = true;
            auto pos = view.find('@');
            if (pos == std::string_view::npos) {
                LogE("parse byte range error");
                break;
            }
            info.range.pos = static_cast<uint64_t>(
                std::strtol(std::string(view.substr(pos + 1)).c_str(), nullptr, 10));
            info.range.size = std::strtol(std::string(view.substr(0, pos)).c_str(), nullptr, 10);
        } else if (view.starts_with("#EXT-X-DISCONTINUITY")) {
            if (!infos_.empty()) {
                infos_.back().isDiscontinuity = true;
            }
        } else if (view.starts_with("#EXT-X-STREAM-INF:")) {
            isPlayList_ = true;
            view = view.substr(18);
            PlayListInfo info;
            // 按 key=value 查找 BANDWIDTH，不依赖属性顺序
            auto findAttr = [](std::string_view attrs, std::string_view key) -> std::string_view {
                auto pos = attrs.find(key);
                if (pos == std::string_view::npos) {
                    return {};
                }
                auto valStart = pos + key.size() + 1; // skip '='
                if (valStart >= attrs.size()) {
                    return {};
                }
                auto valEnd = attrs.find(',', valStart);
                return attrs.substr(valStart,
                                    valEnd == std::string_view::npos ? valEnd : valEnd - valStart);
            };
            auto bandwidth = findAttr(view, "BANDWIDTH");
            if (!bandwidth.empty()) {
                info.codeRate =
                    static_cast<uint32_t>(std::strtol(std::string(bandwidth).c_str(), nullptr, 10));
            }
            playListInfos_.push_back(info);
        } else if (view.starts_with("#EXT-X-ENDLIST")) {
            isCompleted_ = true;
            LogI("parse m3u8 completed.");
        } else if (!view.starts_with("#")) {
            if (isPlayList_) {
                if (playListInfos_.empty()) {
                    LogE("parse play list: info array is empty!");
                    break;
                }
                playListInfos_.back().m3u8Url = spliceUrl(baseUrl_, view);
                auto pos = view.find('.');
                playListInfos_.back().name = (pos == std::string_view::npos)
                                                 ? std::string(view)
                                                 : std::string(view.substr(0, pos));
            } else {
                if (infos_.empty()) {
                    LogE("parse url: info array is empty!");
                    break;
                }
                infos_.back().url = spliceUrl(baseUrl_, view);
            }
        }
    }

    if (!line.empty()) {
        line = DataView();
        buffer.shrink();
    }
    auto view = buffer.view();
    if (view.startWith("#EXT-X-ENDLIST")) {
        isCompleted_ = true;
        LogI("parse m3u8 completed.");
    }
    return isCompleted_;
}

} // namespace slark
