//
// Created by Nevermore on 2021/10/22.
// slark File
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdio>
#include <memory>
#include <string_view>
#include "NonCopyable.h"
#include "FileUtility.h"
#include "Data.hpp"

namespace slark::FileUtil {

using FileHandle = FILE*;

static std::string_view kModeStr[] = {"rb", "wb", "ab", "wb+"};

enum class FileMode {
    ReadMode = 0,
    WriteMode = 1,
    AppendMode = 2,
    FreeMode = 3,
};

class IFile : public NonCopyable {
public:
    explicit IFile(std::string path, FileMode mode);

    ~IFile() override;

public:
    virtual bool open() noexcept;

    virtual void close() noexcept;

    void seek(int64_t offset) noexcept;

    [[nodiscard]] int64_t tell() const noexcept;

    [[nodiscard]] uint64_t fileSize() const noexcept;
public:
    [[nodiscard]] inline bool isOpen() const noexcept {
        return file_ != nullptr;
    }

    [[nodiscard]] inline bool isFailed() const noexcept {
        return isFailed_;
    }
protected:
    std::string path_;
    FileHandle file_ = nullptr;
    FileMode mode_;
    bool isFailed_ = false;
};


class WriteFile : virtual public IFile {
    constexpr static uint32_t kCheckCount = 512;
public:
    explicit WriteFile(std::string path, bool isAppend = false);

    ~WriteFile() override;

    bool write(const Data& data) noexcept;

    bool write(const std::string& str) noexcept;

    bool write(const uint8_t* data, uint64_t size) noexcept;

    void flush() noexcept;

public:
    bool open() noexcept override;

    void close() noexcept override;

protected:
    uint32_t checkEveryN_;
    uint32_t checkCount_;
    uint64_t writeSize_;
    uint32_t writeCount_;
};

class ReadFile : virtual public IFile {
public:
    explicit ReadFile(std::string path);

    ~ReadFile() override;

    std::tuple<bool, std::unique_ptr<Data>> read(uint32_t size) noexcept;

    std::tuple<bool, uint8_t> readByte() noexcept;

    bool read(Data& data) noexcept;

    void backFillByte(uint8_t byte) noexcept;

    [[nodiscard]] inline bool readOver() const noexcept { return readOver_; }

    [[nodiscard]] inline uint64_t readSize() const noexcept { return readSize_; }

public:
    bool open() noexcept override;

    void close() noexcept override;

protected:
    bool readOver_;
    uint64_t readSize_;
};


class File final : public ReadFile, public WriteFile {
public:
    explicit File(const std::string& path);

    ~File() override;

public:
    bool open() noexcept final;

    void close() noexcept final;
};

}//end namespace slark::FileUtil
