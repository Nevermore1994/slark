//
// Created by Nevermore on 2021/10/22.
// slark FileUtil
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "FileUtil.h"

namespace slark::File {

#if !SLARK_IOS && !SLARK_ANDROID
std::string rootPath() noexcept {
    return std::filesystem::current_path().string();
}

std::string tmpPath() noexcept {
    return rootPath() + "/tmp";
}

std::string cachePath() noexcept {
    return rootPath() + "/cache";
}
#endif

bool isDirExist(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path p(path);
    return std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec);
}

bool isFileExist(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path p(path);
    return std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec);
}

bool createDir(std::string_view path) noexcept {
    std::error_code ec;
    return std::filesystem::create_directories(std::filesystem::path(path), ec);
}

bool deleteFile(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::remove(std::filesystem::path(path), ec);
}

bool renameFile(std::string_view oldName, std::string_view newName) noexcept {
    if (oldName.empty() || newName.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(oldName), std::filesystem::path(newName), error);
    return !error;
}

uint64_t fileSize(std::string_view path) noexcept {
    if (path.empty()) {
        return 0;
    }
    std::error_code ec;
    auto size = std::filesystem::file_size(std::filesystem::path(path), ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

bool resizeFile(std::string_view path, uint64_t length) noexcept {
    if (path.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::resize_file(std::filesystem::path(path), length, error);
    return !error;
}

bool removeDir(std::string_view path) noexcept {
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(path), ec);
    return !ec;
}

bool copyFile(std::string_view from, std::string_view to) noexcept {
    std::error_code error;
    std::filesystem::copy(std::filesystem::path(from),
                          std::filesystem::path(to),
                          std::filesystem::copy_options::recursive,
                          error);
    return !error;
}

} // namespace slark::File
