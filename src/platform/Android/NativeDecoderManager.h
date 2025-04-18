//
// Created by Nevermore on 2025/3/6.
//

#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "IDecoder.h"

namespace slark  {

class NativeDecoder: public IDecoder {
public:
    explicit NativeDecoder(DecoderType type)
        : IDecoder(type) {

    }
    ~NativeDecoder() override = default;

    void flush() noexcept override;

    void reset() noexcept override;

    void close() noexcept override;

    AVFramePtr getDecodingFrame(uint64_t pts) noexcept;
protected:
    std::string decoderId_;
    std::mutex mutex_;
    std::unordered_map<uint64_t, AVFrameRefPtr> decodeFrames_;
};

using NativeDecoderRefPtr = std::shared_ptr<NativeDecoder>;

class NativeDecoderManager {
public:
    static NativeDecoderManager& shareInstance() noexcept;

    ~NativeDecoderManager() = default;
public:
    NativeDecoderRefPtr find(const std::string& decoderId) noexcept;

    void add(const std::string& decoderId, NativeDecoderRefPtr ptr) noexcept;

    void remove(const std::string& decoderId) noexcept;
private:
    NativeDecoderManager() = default;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, NativeDecoderRefPtr> decoders_;
};

}