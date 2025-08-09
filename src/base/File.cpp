//
// Created by Nevermore on 2021/10/22.
// slark File
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include <utility>
#include "File.h"
#include "Log.hpp"

namespace slark::File {

IFile::IFile(std::string path, FileMode mode)
    : path_(std::move(path))
    , mode_(mode) {

}

IFile::~IFile() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

bool IFile::open() noexcept {
    if (path_.empty()) {
        return false;
    }
    if (file_) {
        return true;
    }
    file_ = fopen(path_.c_str(), kModeStr[static_cast<int>(mode_)].data());
    if (!file_) {
        isFailed_ = true;
        LogE("open file:{}", std::strerror(errno));
    }
    return file_ != nullptr;
}

void IFile::seek(int64_t offset) noexcept {
    if (file_) {
        fseeko(file_, offset, SEEK_SET);
        LogI("seek to offset:{}, file size:{}", offset, File::fileSize(path_));
    }
}

uint64_t IFile::tell() const noexcept {
    fpos_t pos = 0;
    if (file_ && (fgetpos(file_, &pos) == 0)) {
        return static_cast<uint64_t>(pos);
    }
    return 0;
}

uint64_t IFile::fileSize() const noexcept {
    return File::fileSize(path_);
}

void IFile::close() noexcept {
    if (file_) {
        isFailed_ = false;
        fclose(file_);
        file_ = nullptr;
    }
}


WriteFile::WriteFile(std::string path, bool isAppend)
    : IFile(std::move(path), isAppend ? FileMode::AppendMode : FileMode::WriteMode)
    , checkEveryN_(kCheckCount)
    , checkCount_(0)
    , writeSize_(0)
    , writeCount_(0) {
}

WriteFile::~WriteFile() {
    flush();
    IFile::close();
}

bool WriteFile::open() noexcept {
    writeSize_ = 0;
    writeCount_ = 0;
    return IFile::open();
}

void WriteFile::close() noexcept {
    if (file_) {
        fflush(file_);
    }
    IFile::close();
}

void WriteFile::flush() noexcept {
    if (file_) {
        fflush(file_);
    }
}

bool WriteFile::write(const Data& data) noexcept {
    return write(data.rawData, data.length);
}

bool WriteFile::write(Data&& data) noexcept {
    return write(data.rawData, data.length);
}

bool WriteFile::write(const uint8_t* data, uint64_t size) noexcept {
    if (!file_ || isFailed_) {
        return false;
    }
    writeCount_++;
    auto writeSize = fwrite(data, 1, size, file_);
    writeSize_ += size;
    if ((writeCount_ % checkEveryN_) == 0) {
        flush();
    }
    if (writeSize <= 0) {
        isFailed_ = true;
        LogE("write file: {}", std::strerror(errno));
    }
    return writeSize == size;
}

bool WriteFile::write(const std::string& str) noexcept {
    return write(reinterpret_cast<const uint8_t*>(str.data()), str.length());
}

bool WriteFile::write(std::string&& str) noexcept {
    return write(reinterpret_cast<const uint8_t*>(str.data()), str.length());
}

ReadFile::ReadFile(std::string path)
    : IFile(std::move(path), FileMode::ReadMode)
    , readOver_(false)
    , readSize_(0){
}

ReadFile::~ReadFile() {
    if (file_) {
        readSize_ = 0;
        readOver_ = false;
    }
}

std::expected<uint8_t, bool> ReadFile::readByte() noexcept {
    if (!file_ || readOver_) {
        return std::unexpected(false);
    }
    uint8_t byte = 0;
    auto res = fread(&byte, 1, 1, file_);
    if (feof(file_) || tell() == fileSize()) {
        readOver_ = true;
    }
    if (res == 0 && !readOver_) {
        isFailed_ = true;
        LogE("read byte error:{}", std::strerror(errno));
    }

    readSize_++;
    return byte;
}

std::expected<Data, bool> ReadFile::read(uint32_t size) noexcept {
    if (!file_ || readOver_) {
        return std::unexpected(false);
    }
    Data data(size);
    if (read(data)) {
        return data;
    }
    return std::unexpected(false);
}

bool ReadFile::read(Data& data) noexcept {
    return read(data, data.capacity);
}

bool ReadFile::read(Data& data, uint64_t size) noexcept {
    if (!file_ || readOver_) {
        return false;
    }
    auto res = fread(data.rawData, 1, size, file_);
    if (feof(file_) || tell() == fileSize()) {
        readOver_ = true;
    }
    if (res == 0 && !readOver_) {
        isFailed_ = true;
        LogE("read error:{}", std::strerror(errno));
    }
    data.length = res;
    readSize_ += res;
    return res > 0;
}

void ReadFile::seek(int64_t offset) noexcept {
    IFile::seek(offset);
    readOver_ = false;
    readSize_ = 0;
}

void ReadFile::backFillByte(uint8_t byte) noexcept {
    if (file_) {
        ungetc(byte, file_);
        readSize_--;
    } else {
        LogE("can't back fill byte, file is null");
    }
}

bool ReadFile::open() noexcept {
    readSize_ = 0;
    readOver_ = false;
    return IFile::open();
}


void ReadFile::close() noexcept {
    if (file_) {
        IFile::close();
    }
}

}//end namespace slark::File
