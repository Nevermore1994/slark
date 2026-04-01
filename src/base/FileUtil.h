//
// Created by Nevermore on 2021/10/22.
// slark FileUtil
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace slark::File {

[[maybe_unused]] extern std::string rootPath() noexcept;

[[maybe_unused]] extern std::string tmpPath() noexcept;

[[maybe_unused]] extern std::string cachePath() noexcept;

[[nodiscard]] bool isDirExist(std::string_view path) noexcept;

[[nodiscard]] bool isFileExist(std::string_view path) noexcept;

bool createDir(std::string_view path) noexcept;

bool deleteFile(std::string_view path) noexcept;

bool renameFile(std::string_view oldName, std::string_view newName) noexcept;

[[nodiscard]] uint64_t fileSize(std::string_view path) noexcept;

bool resizeFile(std::string_view path, uint64_t length) noexcept;

bool removeDir(std::string_view path) noexcept;

bool copyFile(std::string_view from, std::string_view to) noexcept;

} // namespace slark::File
