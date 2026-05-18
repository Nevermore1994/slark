//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "File.h"

#include "ByteUtils.hpp"
#include "Log.hpp"

#include <array>
#include <utility>

namespace slark::File {

static constexpr std::array<std::string_view, 4> kModeStr = {"rb", "wb", "ab", "wb+"};

FileBase::FileBase(std::filesystem::path path, FileMode mode)
    : path_(std::move(path)), mode_(mode) {}

FileBase::~FileBase() = default;

Expected<void> FileBase::open() noexcept {
    if (path_.empty()) {
        return std::unexpected(Error::InvalidArgument);
    }
    if (file_) {
        return {};
    }
    auto* fp = fopen(path_.c_str(), kModeStr[static_cast<size_t>(mode_)].data());
    if (!fp) {
        LogE("open file:{}", std::strerror(errno));
        return std::unexpected(Error::FileNotFound);
    }
    file_.reset(fp);
    return {};
}

Expected<void> FileBase::seek(int64_t offset) noexcept {
    if (!file_) {
        return std::unexpected(Error::IOError);
    }
    if (fseeko(file_.get(), offset, SEEK_SET) != 0) {
        return std::unexpected(Error::IOError);
    }
    return {};
}

uint64_t FileBase::tell() const noexcept {
    fpos_t pos = 0;
    if (file_ && (fgetpos(file_.get(), &pos) == 0)) {
        return static_cast<uint64_t>(pos);
    }
    return 0;
}

uint64_t FileBase::fileSize() const noexcept {
    return File::fileSize(path_.string());
}

void FileBase::close() noexcept {
    file_.reset();
}

WriteFile::WriteFile(std::filesystem::path path, bool isAppend)
    : FileBase(std::move(path), isAppend ? FileMode::AppendMode : FileMode::WriteMode) {}

WriteFile::~WriteFile() {
    flush();
    FileBase::close();
}

Expected<void> WriteFile::open() noexcept {
    writeSize_ = 0;
    writeCount_ = 0;
    return FileBase::open();
}

void WriteFile::close() noexcept {
    if (file_) {
        fflush(file_.get());
    }
    FileBase::close();
}

void WriteFile::flush() noexcept {
    if (file_) {
        fflush(file_.get());
    }
}

Expected<uint64_t> WriteFile::write(ConstByteSpan data) noexcept {
    if (!file_) {
        return std::unexpected(Error::IOError);
    }
    writeCount_++;
    auto writeSize = fwrite(data.data(), 1, data.size(), file_.get());
    writeSize_ += writeSize;
    if ((writeCount_ % checkEveryN_) == 0) {
        flush();
    }
    if (writeSize == 0) {
        LogE("write file: {}", std::strerror(errno));
        return std::unexpected(Error::IOError);
    }
    return static_cast<uint64_t>(writeSize);
}

Expected<uint64_t> WriteFile::write(const Data& data) noexcept {
    return write(ConstByteSpan(data.data(), data.length()));
}

Expected<uint64_t> WriteFile::write(std::string_view str) noexcept {
    return write(toByteSpan(str));
}

ReadFile::ReadFile(std::filesystem::path path) : FileBase(std::move(path), FileMode::ReadMode) {}

ReadFile::~ReadFile() = default;

Expected<Byte> ReadFile::readByte() noexcept {
    if (!file_) {
        return std::unexpected(Error::IOError);
    }
    if (readOver_) {
        return std::unexpected(Error::EndOfFile);
    }
    Byte byte = 0;
    auto res = fread(&byte, 1, 1, file_.get());
    if (feof(file_.get()) || tell() == fileSize()) {
        readOver_ = true;
    }
    if (res == 0 && !readOver_) {
        LogE("read byte error:{}", std::strerror(errno));
        return std::unexpected(Error::IOError);
    }
    readSize_++;
    return byte;
}

Expected<Data> ReadFile::read(uint64_t size) noexcept {
    if (!file_) {
        return std::unexpected(Error::IOError);
    }
    if (readOver_) {
        return std::unexpected(Error::EndOfFile);
    }
    Data data(size);
    auto result = readInto(data, size);
    if (!result) {
        return std::unexpected(result.error());
    }
    return data;
}

Expected<uint64_t> ReadFile::readInto(Data& data, uint64_t size) noexcept {
    if (!file_) {
        return std::unexpected(Error::IOError);
    }
    if (readOver_) {
        return std::unexpected(Error::EndOfFile);
    }
    auto res = fread(data.data(), 1, size, file_.get());
    if (feof(file_.get()) || tell() == fileSize()) {
        readOver_ = true;
    }
    if (res == 0 && !readOver_) {
        LogE("read error:{}", std::strerror(errno));
        return std::unexpected(Error::IOError);
    }
    data.setSize(res);
    readSize_ += res;
    return static_cast<uint64_t>(res);
}

Expected<void> ReadFile::seek(int64_t offset) noexcept {
    auto result = FileBase::seek(offset);
    if (result) {
        readOver_ = false;
        readSize_ = 0;
    }
    return result;
}

void ReadFile::backFillByte(Byte byte) noexcept {
    if (file_) {
        ungetc(byte, file_.get());
        if (readSize_ > 0) {
            readSize_--;
        }
    } else {
        LogE("can't back fill byte, file is null");
    }
}

Expected<void> ReadFile::open() noexcept {
    readSize_ = 0;
    readOver_ = false;
    return FileBase::open();
}

void ReadFile::close() noexcept {
    FileBase::close();
}

} // namespace slark::File
