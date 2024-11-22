#pragma once

#include "../tree/tree.hpp"
#include "sgf_exceptions.hpp"
#include "sgf_lexer.hpp"
#include <cstdint>
#include <stack>
#include <stdexcept>
#include <unordered_set>
#include <vector>

class BaseSGFNode : public TreeNode {
public:
    virtual void addProperty(const std::string& tag, const std::vector<std::string>& values) = 0;
};

class StringSGFNode : public BaseSGFNode {
public:
    StringSGFNode() : BaseSGFNode() {}

    void addProperty(const std::string& tag, const std::vector<std::string>& values) override
    {
        content_ += tag;
        tag_value_sizes_.push_back(tag.size());
        is_tag_.push_back(true);
        for (const std::string& value : values) {
            content_ += value;
            tag_value_sizes_.push_back(value.size());
            is_tag_.push_back(false);
        }
    }

    std::string content_;
    std::vector<size_t> tag_value_sizes_;
    std::vector<bool> is_tag_;
};

class BaseNodeAllocator {
public:
    virtual BaseSGFNode* allocate() = 0;

    virtual void deallocate(BaseSGFNode* node) = 0;
};

template <typename NodeType>
class NodeAllocator : public BaseNodeAllocator {
public:
    BaseSGFNode* allocate() override
    {
        return new NodeType();
    }

    void deallocate(BaseSGFNode* node) override
    {
        delete static_cast<NodeType*>(node);
    }
};

template <typename NodeType>
class TrackingNodeAllocator : public BaseNodeAllocator {
public:
    BaseSGFNode* allocate() override
    {
        NodeType* node = new NodeType();
        allocated_nodes.insert(node);
        return node;
    }

    void deallocate(BaseSGFNode* node) override
    {
        if (allocated_nodes.erase(static_cast<NodeType*>(node)) > 0) {
            delete node;
        }
    }

    const std::unordered_set<NodeType*>& getAllocatedNodes() const
    {
        return allocated_nodes;
    }

    void deallocateAll()
    {
        for (NodeType* node : allocated_nodes) {
            delete node;
        }
        allocated_nodes.clear();
    }

private:
    std::unordered_set<NodeType*> allocated_nodes;
};

class SGFParser {
    class DummyNode : public BaseSGFNode {
    public:
        DummyNode() : BaseSGFNode() {}

        void addChild(BaseTreeNode* node) override
        {
            if (child_ != nullptr) {
                throw std::runtime_error("DummyNode can only have one child");
            }
            child_ = node;
        }

        void addProperty(const std::string& tag, const std::vector<std::string>& values) override
        {
            throw std::runtime_error("DummyNode cannot have properties");
        }
    };

    // stack element
    struct Element {
        enum class Type {
            LEFT_PAREN,
            NODE,
        } type;
        size_t start;
        size_t end;
        BaseSGFNode* node;
    };

    // valid next states
    struct NextState {
        static constexpr uint16_t LEFT_PAREN = 1 << 0;
        static constexpr uint16_t RIGHT_PAREN = 1 << 1;
        static constexpr uint16_t SEMICOLON = 1 << 2;
        static constexpr uint16_t TAG = 1 << 3;
        static constexpr uint16_t VALUE = 1 << 4;
    };

public:
    SGFParser(BaseInputStream& input_stream, BaseNodeAllocator& allocator, size_t start = 0, size_t length = 0, std::function<void(size_t, size_t)> progress_callback = nullptr)
        : lexer_(input_stream, start, length, std::move(progress_callback)), allocator_(allocator), dummy_root_(new DummyNode()), current_(dummy_root_)
    {
        next_state_ = NextState::LEFT_PAREN;
    }

    ~SGFParser()
    {
        delete dummy_root_;
    }

    BaseSGFNode* nextNode()
    {
        auto cache_tag = std::string();
        auto cache_values = std::vector<std::string>();

        while (true) {
            const SGFToken& token = lexer_.nextToken();
            if (token.type == SGFTokenType::ENDOFFILE) {
                break;
            }
            switch (token.type) {
                case SGFTokenType::LEFT_PAREN: {
                    if (!(next_state_ & NextState::LEFT_PAREN)) {
                        throw SGFError("Unexpected left parentheses", token.start, token.end);
                    }

                    stack_.push({Element::Type::NODE, 0, 0, current_});
                    stack_.push({Element::Type::LEFT_PAREN, token.start, token.end, nullptr}); // append '(' token to stack

                    // update states
                    next_state_ = NextState::SEMICOLON;
                    break;
                }
                case SGFTokenType::RIGHT_PAREN: {
                    if (!(next_state_ & NextState::RIGHT_PAREN)) {
                        throw SGFError("Unexpected right parentheses", token.start, token.end);
                    }

                    if (stack_.empty()) {
                        throw SGFError("Unmatched right parentheses", token.start, token.end);
                    }

                    // store tag and value to current node if needed
                    BaseSGFNode* return_node = nullptr;
                    if (!cache_values.empty()) {
                        current_->addProperty(cache_tag, cache_values);
                        cache_values.clear(); // not needed because the cache will be released after the node is returned
                        return_node = current_;
                    }

                    // pop until '('
                    while (true) {
                        if (stack_.empty()) {
                            throw SGFError("Unmatched right parentheses", token.start, token.end);
                        }
                        if (stack_.top().type == Element::Type::LEFT_PAREN) {
                            stack_.pop(); // pop '(' token
                            break;
                        }
                        stack_.pop(); // pop node
                    }
                    current_ = stack_.top().node; // pop the node before '('
                    stack_.pop();

                    // update states
                    next_state_ = NextState::LEFT_PAREN | NextState::RIGHT_PAREN;

                    // return the node if needed
                    if (return_node != nullptr) {
                        return return_node;
                    }
                    break;
                }
                case SGFTokenType::SEMICOLON: {
                    if (!(next_state_ & NextState::SEMICOLON)) {
                        throw SGFError("Unexpected semicolon", token.start, token.end);
                    }

                    // store tag and value to current node if needed
                    BaseSGFNode* return_node = nullptr;
                    if (!cache_values.empty()) {
                        current_->addProperty(cache_tag, cache_values);
                        // cache_values.clear();  // not needed because the cache will be released after the node is returned
                        return_node = current_;
                    }

                    // create a new node
                    stack_.push({Element::Type::NODE, 0, 0, current_});
                    current_ = allocator_.allocate();
                    stack_.top().node->addChild(current_);

                    // update states
                    next_state_ = NextState::TAG;

                    // return the node if needed
                    if (return_node != nullptr) {
                        return return_node;
                    }
                    break;
                }
                case SGFTokenType::TAG: {
                    if (!(next_state_ & NextState::TAG)) {
                        throw SGFError("Unexpected tag " + token.value, token.start, token.end);
                    }

                    // store tag and value to current node if needed
                    if (!cache_values.empty()) {
                        current_->addProperty(cache_tag, cache_values);
                        cache_values.clear();
                    }

                    cache_tag = token.value; // cache the tag, will be used when the value comes

                    // update states
                    next_state_ = NextState::VALUE;
                    break;
                }
                case SGFTokenType::VALUE: {
                    if (!(next_state_ & NextState::VALUE)) {
                        throw SGFError("Unexpected value " + token.value, token.start, token.end);
                    }

                    cache_values.push_back(token.value);

                    // update states
                    next_state_ = NextState::LEFT_PAREN | NextState::RIGHT_PAREN | NextState::SEMICOLON | NextState::TAG | NextState::VALUE;
                    break;
                }
                case SGFTokenType::IGNORE:
                    break;
                default:
                    throw SGFError("Unexpected token " + token.value, token.start, token.end);
                    break;
            }
        }

        // make sure all the parentheses are matched
        if (!stack_.empty()) {
            // pop until the first '(' token
            Element last_left_paren;
            while (!stack_.empty()) {
                if (stack_.top().type == Element::Type::LEFT_PAREN) {
                    last_left_paren = stack_.top();
                    stack_.pop();
                    break;
                }
                stack_.pop();
            }
            throw SGFError("Unmatched left parentheses", last_left_paren.start, last_left_paren.end);
        }

        // remove the dummy root
        BaseSGFNode* root_child = static_cast<BaseSGFNode*>(dummy_root_->child_);
        if (root_child != nullptr) {
            root_child->detach();
        }

        return nullptr;
    }

private:
    SGFLexer lexer_;
    BaseNodeAllocator& allocator_;
    std::stack<Element> stack_;
    BaseSGFNode* dummy_root_;
    BaseSGFNode* current_;
    uint16_t next_state_ = 0;
};
