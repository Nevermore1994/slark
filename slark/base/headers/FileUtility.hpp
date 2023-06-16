//
// Created by Nevermore on 2021/10/22.
// Slark FileUtility
// Copyright (c) 2021 Nevermore All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>

namespace Slark::FileUtil {

constexpr int64_t kFileError = -1l;

bool isDirExist(const std::string&);

bool isFileExist(const std::string&);

bool createDir(const std::string&);

bool deleteFile(const std::string&);

bool renameFile(const std::string& oldName, const std::string& newName);

int64_t fileSize(const std::string& path);

bool resizeFile(const std::string& path, uint64_t length);

bool removeDir(const std::string& path);

bool copyFile(const std::string& from, const std::string& to);

}



