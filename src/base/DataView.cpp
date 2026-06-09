//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DataView.hpp"

#include <ranges>

namespace slark {

std::vector<DataView> DataView::split(std::string_view delimiter) const noexcept {
    return StringUtil::split(toStringView(), delimiter) | std::views::transform([](auto view) {
               return DataView::fromString(view);
           }) |
           std::ranges::to<std::vector>();
}

std::string DataView::removeSpace() const noexcept {
    return StringUtil::removeSpace(toStringView());
}

} // namespace slark
