#include "demuxer/DemuxerManager.h"

#include <gtest/gtest.h>

#include "demuxer/IDemuxer.h"

using namespace slark;

TEST(DemuxerManagerTest, SingletonInstance) {
    auto& mgr1 = DemuxerManager::shareInstance();
    auto& mgr2 = DemuxerManager::shareInstance();
    EXPECT_EQ(&mgr1, &mgr2);
}

TEST(DemuxerManagerTest, InitRegistersDemuxers) {
    auto& mgr = DemuxerManager::shareInstance();
    mgr.init();
    EXPECT_TRUE(mgr.contains(DemuxerType::MP4));
    EXPECT_TRUE(mgr.contains(DemuxerType::WAV));
}

TEST(DemuxerManagerTest, CreateDemuxer) {
    auto& mgr = DemuxerManager::shareInstance();
    mgr.init();

    auto mp4 = mgr.create(DemuxerType::MP4);
    EXPECT_NE(mp4, nullptr);
    EXPECT_EQ(mp4->type(), DemuxerType::MP4);

    auto wav = mgr.create(DemuxerType::WAV);
    EXPECT_NE(wav, nullptr);
    EXPECT_EQ(wav->type(), DemuxerType::WAV);
}

TEST(DemuxerManagerTest, UnknownTypeReturnsNull) {
    auto& mgr = DemuxerManager::shareInstance();
    auto demuxer = mgr.create(static_cast<DemuxerType>(99));
    EXPECT_EQ(demuxer, nullptr);
}
