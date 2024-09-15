
//
//  DemuxerManager.hpp
//  slark
//
//  Created by Nevermore on 2022/5/2.
//

#pragma once

#include "IDemuxer.h"
#include "NonCopyable.h"
#include <memory>
#include <unordered_set>

namespace slark {

class DemuxerManager : public slark::NonCopyable {

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

    DemuxerType probeDemuxType(std::string_view str) const noexcept;

    std::unique_ptr<IDemuxer> create(DemuxerType type) const noexcept;

private:
    std::unordered_map<DemuxerType, DemuxerInfo> demuxers_;
};

}

/* DemuxerManager_hpp */
