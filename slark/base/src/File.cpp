//
// Created by Nevermore on 2021/10/22.
// Slark File
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "File.hpp"
#include "Log.hpp"
#include <utility>
#include <cstring>

namespace Slark::FileUtil {

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
    file_ = fopen(path_.c_str(), ModeStr[static_cast<int>(mode_)]);
    if (!file_) {
        LogE("open file:%s", std::strerror(errno));
        isFailed_ = true;
    }
    return file_ != nullptr;
}

void IFile::seek(int64_t offset) noexcept {
    if (file_) {
        fseeko(file_, offset, SEEK_SET);
    }
}

int64_t IFile::tell() const noexcept {
    return file_ ? ftello(file_) : 0;
}

uint64_t IFile::fileSize() const noexcept {
    return FileUtil::fileSize(path_);
}

void IFile::close() noexcept {
    if (file_) {
        isFailed_ = false;
        fclose(file_);
        file_ = nullptr;
    }
}


WriteFile::WriteFile(std::string path)
    : IFile(std::move(path), FileMode::WriteMode)
    , checkEveryN_(kCheckCount)
    , checkCount_(0)
    , writeSize_(0)
    , writeCount_(0) {
}

WriteFile::~WriteFile() {
    flush();
}

bool WriteFile::open() noexcept {
    writeSize_ = 0;
    writeCount_ = 0;
    return IFile::open();
}

void WriteFile::close() noexcept {
    if (file_) {
        IFile::close();
        fflush(file_);
        writeSize_ = 0;
        writeCount_ = 0;
    }
}

void WriteFile::flush() noexcept {
    if (file_) {
        fflush(file_);
        writeCount_ = 0;
    }
}

bool WriteFile::write(const Data& data) noexcept {
    return write(data.data, data.length);
}

bool WriteFile::write(const uint8_t* data, uint64_t size) noexcept {
    if (!file_ || isFailed_) {
        return false;
    }
    writeCount_++;
    auto writeSize = fwrite(data, size, 1, file_);
    writeSize_ += writeSize;
    if (writeCount_ >= checkEveryN_) {
        flush();
    }
    if (writeSize < size) {
        LogE("write file: %s", std::strerror(errno));
        isFailed_ = true;
    }
    return writeSize == size;
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

std::tuple<bool, uint8_t> ReadFile::readByte() noexcept {
    if (!file_ || readOver_) {
        return {false, 0};
    }
    uint8_t byte = 0;
    auto res = fread(&byte, 1, 1, file_);
    if (res == 0) {
        if (feof(file_)) {
            readOver_ = true;
        } else {
            LogE("read byte error:%s", std::strerror(errno));
            isFailed_ = true;
        }
    }
    readSize_++;
    return {res == 1, byte};
}

std::tuple<bool, Data> ReadFile::read(uint32_t size) noexcept {
    Data data;
    if (!file_ || readOver_) {
        return {false, data};
    }
    data = Data(size);
    auto res = read(data);
    return {res, data};
}

bool ReadFile::read(Data& data) noexcept {
    if (!file_ || readOver_) {
        return false;
    }
    auto res = fread(&data.data, data.capacity, 1, file_);
    if (res < data.capacity) {
        if (feof(file_)) {
            readOver_ = true;
        } else {
            LogE("read data error:%s", std::strerror(errno));
            isFailed_ = true;
        }
    }
    data.length = res;
    return res > 0;
}

void ReadFile::backfillByte(uint8_t byte) noexcept {
    if (file_) {
        ungetc(byte, file_);
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
        readSize_ = 0;
        readOver_ = false;
    }
}

File::File(std::string path)
    : IFile(path, FileMode::FreeMode)
    , ReadFile(path)
    , WriteFile(path) {
}

File::~File() = default;

bool File::open() noexcept {
    readSize_ = 0;
    readOver_ = false;
    return WriteFile::open();
}

void File::close() noexcept {
    WriteFile::close();
    readSize_ = 0;
    readOver_ = false;
}

}//end namespace Slark::FileUtil
