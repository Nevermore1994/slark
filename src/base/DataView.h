//
//  DataView.h
//  slark
//
//  Created by Nevermore on 2025/1/9.
//
#pragma once
#include <string_view>
#include <sstream>
#include <ranges>
#include "Assert.hpp"
#include "StringUtil.h"
#include "Range.h"

namespace slark {

class DataView {
public:
    DataView() = default;
    
    explicit DataView(const std::string& str)
        : view_(std::string_view(str)){
        
    }
    
    explicit DataView(std::string_view sv)
        : view_(sv){
        
    }
    
    DataView(const DataView& view) = default;
    
    DataView(const uint8_t* data, uint64_t size)
        : view_(std::string_view(
                reinterpret_cast<const char*>(data),
                static_cast<size_t>(size)
                )){
        
    }
    
    DataView& operator=(const DataView& rhs) = default;
    
    uint8_t operator[](uint64_t index) const noexcept {
        SAssert(index < length(), "out of range!");
        return static_cast<uint8_t>(view_[index]);
    }
    
    uint64_t length() const noexcept {
        return static_cast<uint64_t>(view_.length());
    }
    
    size_t size() const noexcept {
        return view_.size();
    }
    
    bool empty() const noexcept {
        return view_.empty();
    }
    
    DataView substr(uint64_t pos, size_t size = std::string_view::npos) const noexcept {
        if (pos >= length()) {
            return {};
        }
        return DataView(view_.substr(pos, size));
    }

    DataView substr(Range range) const noexcept {
        if (!range.isValid()) {
            return {};
        }
        uint64_t pos = range.pos.value_or(0);
        if (pos >= length()) {
            return {};
        }
        if (!range.size.has_value() || range.size.value_or(0) == INT64_MAX) {
            return DataView(view_.substr(pos));
        }
        if (range.end() > length()) {
            return DataView(view_.substr(pos));
        }
        return DataView(view_.substr(pos, static_cast<size_t>(range.size.value_or(0))));
    }
    
    bool operator==(const DataView& view) const noexcept {
        return view_ == view.view_;
    }
    
    std::string_view view() const noexcept {
        return view_;
    }
    
    size_t find(std::string_view view) const noexcept {
        return view_.find(view);
    }
    
    size_t find(char c) const noexcept {
        return view_.find(c);
    }
    
    std::string str() const noexcept {
        return std::string(view_);
    }
    
    int compare(size_t pos, size_t size, DataView data) const {
        return view_.compare(pos, size, data.view_);
    }
    
    int compare(size_t pos, size_t size, std::string_view data) const {
        return view_.compare(pos, size, data);
    }
    
    int compare(size_t pos, size_t size, const char* s) const {
        return view_.compare(pos, size, s);
    }
    
    bool startWith(std::string_view view) const noexcept {
        return view_.starts_with(view);
    }
    
    std::vector<DataView> split(std::string_view delimiter) const noexcept {
        return StringUtil::split(view_, delimiter)
            | std::views::transform([](auto view){ return DataView(view);})
            | std::ranges::to<std::vector>();
    }
    
    DataView& removePrefix(char c) noexcept {
        while (view_.starts_with(c)) {
            view_.remove_prefix(1);
        }
        return *this;
    }
    
    std::string removeSpace() noexcept {
        return StringUtil::removeSpace(view_);
    }
private:
    std::string_view view_;
};

std::ostringstream& operator<<(std::ostringstream& stream, const DataView& view) noexcept;

} //end of namespace slark

