
//
//  DemuxerManager.hpp
//  Slark
//
//  Created by Nevermore on 2022/5/2.
//

#pragma once

#include "IDemuxer.hpp"
#include "NonCopyable.hpp"
#include <memory>
#include <unordered_set>

namespace Slark {

class DemuxerManager : public Slark::NonCopyable {

public:
    inline static DemuxerManager& shareInstance() {
        static DemuxerManager instance;
        return instance;
    }

    DemuxerManager();

    ~DemuxerManager() override = default;

public:
    void init() noexcept;

    bool contains(DemuxerType type) const noexcept;

    DemuxerType probeDemuxType(const std::string& str) const noexcept;

    DemuxerType probeDemuxType(std::unique_ptr<Data> data) const noexcept;

    DemuxerType probeDemuxType(const std::string_view& str) const noexcept;

    std::unique_ptr<IDemuxer> create(DemuxerType type) noexcept;

private:
    std::unordered_map<std::string_view, DemuxerInfo> demuxers_;
};

}

/* DemuxerManager_hpp */
