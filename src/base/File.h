//
// Created by Nevermore on 2021/10/22.
// slark File
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include "Data.h"
#include "FileUtil.h"
#include "NonCopyable.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

namespace slark::File {

enum class FileMode : uint8_t {
    ReadMode = 0,
    WriteMode = 1,
    AppendMode = 2,
    FreeMode = 3,
};

class FileBase : public NonCopyMovable {
public:
    explicit FileBase(std::filesystem::path path, FileMode mode);

    virtual ~FileBase();

    virtual Expected<void> open() noexcept;

    virtual void close() noexcept;

    virtual Expected<void> seek(int64_t offset) noexcept;

    [[nodiscard]] uint64_t tell() const noexcept;

    [[nodiscard]] uint64_t fileSize() const noexcept;

    [[nodiscard]] bool isOpen() const noexcept {
        return file_ != nullptr;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

protected:
    std::filesystem::path path_;
    std::unique_ptr<FILE, decltype(&fclose)> file_{nullptr, &fclose};
    FileMode mode_ = FileMode::ReadMode;
};

class WriteFile : public FileBase {
    static constexpr uint32_t kCheckCount = 512;

public:
    explicit WriteFile(std::filesystem::path path, bool isAppend = false);

    ~WriteFile() override;

    Expected<uint64_t> write(ConstByteSpan data) noexcept;

    Expected<uint64_t> write(const Data& data) noexcept;

    Expected<uint64_t> write(std::string_view str) noexcept;

    void flush() noexcept;

    [[nodiscard]] uint32_t writeCount() const noexcept {
        return writeCount_;
    }

    [[nodiscard]] uint64_t writeSize() const noexcept {
        return writeSize_;
    }

    Expected<void> open() noexcept override;

    void close() noexcept override;

private:
    uint32_t checkEveryN_ = kCheckCount;
    uint32_t checkCount_ = 0;
    uint64_t writeSize_ = 0;
    uint32_t writeCount_ = 0;
};

class ReadFile : public FileBase {
public:
    explicit ReadFile(std::filesystem::path path);

    ~ReadFile() override;

    Expected<Data> read(uint64_t size) noexcept;

    Expected<Byte> readByte() noexcept;

    Expected<uint64_t> readInto(Data& data, uint64_t size) noexcept;

    Expected<void> seek(int64_t offset) noexcept override;

    void backFillByte(Byte byte) noexcept;

    [[nodiscard]] bool readOver() const noexcept {
        return readOver_;
    }

    [[nodiscard]] uint64_t readSize() const noexcept {
        return readSize_;
    }

    Expected<void> open() noexcept override;

    void close() noexcept override;

private:
    bool readOver_ = false;
    uint64_t readSize_ = 0;
};

} // namespace slark::File
