//
// Created by Nevermore on 2021/10/22.
// slark FileUtil
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "FileUtil.h"
#include <filesystem>

namespace slark::FileUtil  {

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

bool isDirExist(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

bool isFileExist(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    return std::filesystem::exists(path) && !std::filesystem::is_directory(path);
}

bool createDir(const std::string& path) noexcept {
    return std::filesystem::create_directories(path);
}

bool deleteFile(const std::string& path) noexcept {
    return !path.empty() && std::filesystem::remove(path);
}

bool renameFile(const std::string& oldName, const std::string& newName) noexcept {
    if (oldName.empty() || newName.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::rename(oldName, newName, error);
    return !error;
}

uint64_t fileSize(const std::string& path) noexcept {
    return path.empty() ? 0 : static_cast<uint64_t>(std::filesystem::file_size(path));
}

bool resizeFile(const std::string& path, uint64_t length) noexcept {
    if (path.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::resize_file(path, length, error);
    return !error;
}

bool removeDir(const std::string& path) noexcept {
    return path.empty() || std::filesystem::remove_all(path);
}

bool copyFile(const std::string& from, const std::string& to) noexcept {
    std::error_code error;
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, error);
    return !error;
}

}//end namespace slark
