//
// Created by Nevermore on 2021/10/22.
// slark FileUtility
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "FileUtility.hpp"
#include <cstdio>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <filesystem>

namespace slark {

bool FileUtil::isDirExist(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool FileUtil::isFileExist(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool FileUtil::createDir(const std::string& path) {
    return std::filesystem::create_directory(path);
}

bool FileUtil::deleteFile(const std::string& path) {
    return remove(path.c_str()) == 0;
}

bool FileUtil::renameFile(const std::string& oldName, const std::string& newName) {
    return rename(oldName.c_str(), newName.c_str()) == 0;
}

int64_t FileUtil::getFileSize(const std::string& path) {
    struct stat statBuffer{};
    if(stat(path.c_str(), &statBuffer) == 0) {
        return statBuffer.st_size;
    }
    return -1;
}

bool FileUtil::resizeFile(const std::string& path, uint64_t length) {
    return truncate(path.c_str(), length) == 0;
}

bool FileUtil::removeDir(const std::string& path, bool isRetain) {
    if(path.empty()) {
        return true;
    }
    DIR *dir = nullptr;
    struct dirent *dirInfo = nullptr;
    struct stat statBuffer{};
    lstat(path.c_str(), &statBuffer);
    bool res = true;
    
    if(S_ISREG(statBuffer.st_mode)) {
        remove(path.c_str());
    } else if(S_ISDIR(statBuffer.st_mode)) {
        if((dir = opendir(path.c_str())) == nullptr) {
            return true;
        }
        while((dirInfo = readdir(dir)) != nullptr) {
            std::string filePath = path;
            filePath.append("/");
            filePath.append(dirInfo->d_name);
            if(strcmp(dirInfo->d_name, ".") == 0 || strcmp(dirInfo->d_name, "..") == 0) {
                continue;
            }
            res = res && removeDir(filePath);
            if(isRetain) {
                remove(filePath.c_str());
            }
        }
        closedir(dir);
        if(!isRetain) {
            remove(path.c_str());
        }
    }
    return res;
}

bool FileUtil::copyFile(const std::string &from, const std::string &to){
    std::error_code error;
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, error);
    return error.value() == 0;
}

}//end namespace slark
