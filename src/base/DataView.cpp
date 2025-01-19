//
//  DataView.cpp
//  slark
//
//  Created by Nevermore on 2025/1/10.
//

#include "DataView.h"

namespace slark {

std::ostringstream& operator<<(std::ostringstream& stream, const DataView& view) noexcept {
    stream << view.view();
    return stream;
}

}
