//
//  Node.h
//  slark
//
//  Created by Nevermore.
//

#pragma once
#include <unordered_map>
#include <memory>
#include "AVFrame.hpp"

namespace slark {

class INode {
public:
    INode() = default;
    virtual ~INode() = default;
    virtual void process(std::shared_ptr<AVFrame> frame) noexcept = 0;
};

class InputNode: public virtual INode {
public:
    ~InputNode() override = default;
    virtual void send(std::shared_ptr<AVFrame> frame) noexcept = 0;
};

class OutputNode: public virtual INode {
public:
    ~OutputNode() override = default;

    virtual void receive(std::shared_ptr<AVFrame> frame) noexcept;
    bool addTarget(std::weak_ptr<InputNode> node) noexcept;
    bool removeTarget(const std::weak_ptr<InputNode>& node) noexcept;
    void removeAllTarget() noexcept;
protected:
    void notifyTargets(std::shared_ptr<AVFrame> frame) noexcept;
protected:
    std::unordered_map<uint64_t, std::weak_ptr<InputNode>> targets_;
};

class Node: public InputNode, public OutputNode {
public:
    ~Node() override = default;
    void send(std::shared_ptr<AVFrame> frame) noexcept override;
    void process(std::shared_ptr<AVFrame> frame) noexcept override;
};
}
