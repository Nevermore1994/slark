//
// Created by Nevermore on 2021/10/22.
// slark FileUtil
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>

namespace slark::FileUtil {

bool isDirExist(const std::string&) noexcept;

bool isFileExist(const std::string&) noexcept;

bool createDir(const std::string&) noexcept;

bool deleteFile(const std::string&) noexcept;

bool renameFile(const std::string& oldName, const std::string& newName) noexcept;

uint64_t fileSize(const std::string& path) noexcept;

bool resizeFile(const std::string& path, uint64_t length) noexcept;

bool removeDir(const std::string& path) noexcept;

bool copyFile(const std::string& from, const std::string& to) noexcept;

}



