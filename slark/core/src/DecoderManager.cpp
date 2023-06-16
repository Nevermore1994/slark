//
//  DecoderManager.cpp
//  Slark
//
//  Created by Nevermore on 2022/4/24.
//

#include "DecoderManager.hpp"

namespace Slark {

void DecoderManager::init() noexcept {

}

std::unique_ptr<IDecoder> DecoderManager::create(DecoderType type) noexcept {
    return nullptr;
}

bool DecoderManager::contains(DecoderType type) noexcept {
    //return decoders_.count(type) != 0;
    return false;
}


}//end namespace Slark
