//
// Created by Nevermore on 2021/10/22.
// slark File
// Copyright (c) 2021 Nevermore All rights reserved.
//

#include "File.hpp"
#include <cstdio>

namespace slark::FileUtil{

File::File(const std::string& path, std::ios_base::openmode mode)
    : path_(path)
    , mode_(mode) {
    
}

File::File(std::string&& path, std::ios_base::openmode mode)
    : path_(path)
    , mode_(mode) {
    
}

File::~File(){
    close();
}

bool File::open(){
    if(file_ == nullptr){
        file_ = std::make_unique<std::fstream>(path_, static_cast<std::ios_base::openmode>(mode_));
    }
    return file_->is_open();
}

void File::close(){
    if(file_){
        file_->flush();
        file_->close();
        file_.reset();
    }
}

}//end namespace slark::FileUtil
