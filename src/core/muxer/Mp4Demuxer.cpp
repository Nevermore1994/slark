//
//  Mp4Demuxer.cpp
//  slark
//
//  Created by Nevermore
//

#include <stack>
#include "Mp4Demuxer.hpp"
#include "Log.hpp"
#include "Util.hpp"

namespace slark {

bool isContainerBox(std::string_view symbol) noexcept {
    using namespace std::string_view_literals;
    static const std::vector<std::string_view> kContainerBox = {
        "moov"sv,
        "trak"sv,
        "edts"sv,
        "mdia"sv,
        "meta"sv,
        "minf"sv,
        "stbl"sv,
        "dinf"sv,
        "udta"sv,
    };
    return std::any_of(kContainerBox.begin(), kContainerBox.end(), [symbol](const auto& view) {
        return view == symbol;
    });
}

bool Mp4Demuxer::probeMoovBox(Buffer& buffer, int64_t& start, uint32_t& size) noexcept {
    auto view = buffer.view();
    auto pos = view.find("moov");
    if (pos == std::string_view::npos) {
        return false;
    }
    auto boxStart = pos - 4;
    start = boxStart + buffer.offset(); //size start pos
    if (start < 0) {
        return false;
    }
    auto sizeView = view.substr(boxStart, 4);
    return Util::read4ByteBE(sizeView, size);
}

bool Mp4Demuxer::parseMoovBox(Buffer& buffer, BoxRefPtr moovBox) noexcept {
    std::stack<BoxRefPtr> parentBox;
    parentBox.push(moovBox);
    auto size = moovBox->info.size - moovBox->info.headerSize;
    if (!buffer.require(size)) {
        return false;
    }
    bool res = true;
    while ((buffer.pos() - moovBox->info.start) < moovBox->info.size) {
        auto box = Box::createBox(buffer);
        if (!box) {
            res = false;
            break;
        }
        
        box->decode(buffer);
        if (box->info.symbol == "stsd") {
            auto stsdBox = std::dynamic_pointer_cast<BoxStsd>(box);
            SAssert(stsdBox != nullptr, "parse error");
            auto codecId = stsdBox->getCodecId();
            if (codecId == CodecId::Unknown) {
                LogI("Unknown codec!");
            } else {
                auto mdhdBox = std::dynamic_pointer_cast<BoxMdhd>(moovBox->getChild("mdhd"));
                tracks_[codecId] = std::make_shared<TrackContext>();
                tracks_[codecId]->stbl = parentBox.top();
                tracks_[codecId]->mdhd = mdhdBox;
            }
        }
        
        parentBox.top()->append(box);
        if (isContainerBox(box->info.symbol)) {
            parentBox.push(box);
            continue;
        } else {
            auto endPos = box->info.size + box->info.start;
            auto skipOffset = box->info.start + box->info.size - buffer.pos();
            if (!buffer.skip(skipOffset)) {
                res = false;
                break;
            }
            while(!parentBox.empty()) {
                auto topBox = parentBox.top();
                if (endPos >= (topBox->info.start + topBox->info.size)) {
                    parentBox.pop();
                } else {
                    break;
                }
            }
        }
    }
    return true;
}

bool Mp4Demuxer::open(std::unique_ptr<Buffer>& buffer) noexcept {
    if (rootBox_ == nullptr) {
        BoxInfo info;
        info.start = 0;
        info.size = buffer->totalSize();
        rootBox_ = std::make_shared<Box>(std::move(info));
    }
    auto res = false;
    while (!buffer->empty()) {
        auto box = Box::createBox(*buffer);
        if (!box) {
            break;
        }
        
        if (box->info.symbol == "moov") {
            res = parseMoovBox(*buffer, box);
            if (!res) {
                break;
            }
            rootBox_->append(box);
        } else if (box->info.symbol == "mdat") {
            headerInfo_ = std::make_unique<DemuxerHeaderInfo>();
            headerInfo_->headerLength = box->info.start;
            headerInfo_->dataSize = box->info.size;
            rootBox_->append(box);
        } else {
            rootBox_->append(box);
        }
        
        auto endPos = box->info.size + box->info.start;
        auto skipOffset = box->info.start + box->info.size - buffer->pos();
        if (!buffer->skip(skipOffset)) {
            if (box->info.symbol != "mdat") {
                res = false;
            }
            break;
        }
    }
    
    if (!res) {
        buffer->resetReadPos();
    }
    return res;
}

void Mp4Demuxer::close() noexcept {
    
}

DemuxerResult Mp4Demuxer::parseData(std::unique_ptr<Data> data, int64_t offset) noexcept {
    if (data->empty() || !isInited_) {
        return {DemuxerResultCode::Failed, AVFrameArray(), AVFrameArray()};
    } else if (!buffer_->append(static_cast<uint64_t>(offset), std::move(data))) {
        return {DemuxerResultCode::Normal, AVFrameArray(), AVFrameArray()};
    }
    return {};
}

uint64_t Mp4Demuxer::getSeekToPos(Time::TimePoint) noexcept {
    return 0;
}

}

