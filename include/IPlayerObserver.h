#pragma once

#include <memory>

namespace slark {

struct IPlayerObserver;
using IPlayerObserverPtr = std::weak_ptr<IPlayerObserver>;
using IPlayerObserverRefPtr = std::shared_ptr<IPlayerObserver>;

} // namespace slark
