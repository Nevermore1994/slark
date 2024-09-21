//
// Created by Nevermore on 2021/10/22.
// slark FileUtil
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>

namespace slark::FileUtil {

[[maybe_unused]] extern std::string rootPath() noexcept;

[[maybe_unused]] extern std::string tmpPath() noexcept;

[[maybe_unused]] extern std::string cachePath() noexcept;

[[maybe_unused]] bool isDirExist(const std::string&) noexcept;

[[maybe_unused]] bool isFileExist(const std::string&) noexcept;

[[maybe_unused]] bool createDir(const std::string&) noexcept;

[[maybe_unused]] bool deleteFile(const std::string&) noexcept;

[[maybe_unused]] bool renameFile(const std::string& oldName, const std::string& newName) noexcept;

[[maybe_unused]] uint64_t fileSize(const std::string& path) noexcept;

[[maybe_unused]] bool resizeFile(const std::string& path, uint64_t length) noexcept;

[[maybe_unused]] bool removeDir(const std::string& path) noexcept;

[[maybe_unused]] bool copyFile(const std::string& from, const std::string& to) noexcept;

}



