//
//  Mp4Box.cpp
//  slark
//
//  Created by Nevermore
//

#include <format>
#include "Mp4Box.hpp"
#include "Util.hpp"

namespace slark {

bool Box::decode(std::string_view ) {
    return 0;
}

std::string Box::description(const std::string& prefix) const noexcept {
    return std::format("{} {} [{}, {})", prefix, Util::uint32ToByteBE(type), start, start + size);
}

void Box::append(std::shared_ptr<Box> child) noexcept {
    childs.push_back(std::move(child));
}

BoxRefPtr Box::getChild(std::string_view childType) const noexcept {
    for (const auto& child : childs) {
        if (Util::uint32ToByteBE(child->type) == childType) {
            return child;
        }
    }
    return nullptr;
}

BoxRefPtr Box::createBox(std::string_view sv) {
    if (sv.size() < 8) {
        return nullptr;
    }

    return nullptr;
}

}
