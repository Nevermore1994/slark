//
//  Node.hpp
//  slark
//
//  Created by Nevermore on 2022/8/24.
//

#pragma once
#include <unordered_map>
#include <memory>
#include "AVFrame.hpp"

namespace slark{

class INode {
public:
    INode() = default;
    virtual ~INode() = default;
    virtual void process() noexcept = 0;
protected:
    std::shared_ptr<AVFrame> frame_;
};

class InputNode: public virtual INode{
public:
    ~InputNode() override = default;
    void receive(std::shared_ptr<AVFrame> frame) noexcept;
    void process() noexcept override;
};

class OutputNode: public virtual INode {
public:
    ~OutputNode() override = default;

    bool addTarget(std::weak_ptr<InputNode> node) noexcept;
    bool removeTarget(const std::weak_ptr<InputNode>& node) noexcept;
    void removeAllTarget() noexcept;
    void process() noexcept override;
protected:
    void notifyTargets() noexcept;
protected:
    std::unordered_map<uint64_t, std::weak_ptr<InputNode>> targets_;
};

class Node: public InputNode, public OutputNode {
public:
    ~Node() override = default;
    void process() noexcept override;
};
}
