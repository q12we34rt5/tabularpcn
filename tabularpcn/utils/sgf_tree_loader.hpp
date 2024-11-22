#pragma once
#include "sgf_parser.hpp"
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma pack(push, 1)
class SGFTreeNode : public BaseSGFNode {
public:
    void addProperty(const std::string& tag, const std::vector<std::string>& values) override
    {
        if (tag == "B") {
            assert(values.size() == 1);
            type_ = Type::OR;
        }
        if (tag == "W") {
            assert(values.size() == 1);
            type_ = Type::AND;
        }
        if (tag == "C") {
            assert(values.size() == 1);
            static auto get_property = [](const std::string& comment, const std::string& key) -> std::string {
                size_t pos = comment.find(key);
                if (pos == std::string::npos) {
                    return "";
                }
                pos += key.size();
                size_t end_pos = comment.find('\n', pos);
                if (end_pos == std::string::npos) {
                    end_pos = comment.size();
                } else if (end_pos > 0 && comment[end_pos - 1] == '\r') {
                    --end_pos;
                }
                return comment.substr(pos, end_pos - pos);
            };
            auto& comment = values[0];
            std::string solver_status = get_property(comment, "solver_status: ");
            if (solver_status == "WIN" || solver_status == "LOSS") {
                solved_ = true;
            }
            match_tt_ = get_property(comment, "match_tt = ") == "true";
            assert(!match_tt_ || solved_);
            pruned_by_rzone_ = get_property(comment, "equal_loss = ") != "-1";
            assert(!pruned_by_rzone_ || solved_);
        }

        properties_.push_back({tag, values});
    }

    std::string toString() const
    {
        std::ostringstream oss;
        oss << "SGFTreeNode("
            << "id=" << id_ << ", "
            << "type=" << typeToString(type_) << ", "
            << "tree_size=" << tree_size_ << ", "
            << "proof_tree_size=" << proof_tree_size_ << ", "
            << "solved=" << (solved_ ? "true" : "false")
            << ")";
        return oss.str();
    }

    std::string toSgfString() const
    {
        std::ostringstream oss;
        return _toSgfString(oss).str();
    }

    std::string toSgf() const
    {
        std::ostringstream oss;
        return _toSgf(oss, true).str();
    }

private:
    std::ostringstream& _toSgfString(std::ostringstream& oss) const
    {
        oss << ";";
        for (const auto& [tag, values] : properties_) {
            oss << tag;
            if (tag != "C") {
                for (const auto& value : values) {
                    oss << "[" << value << "]";
                }
            } else {
                oss << "[" << values[0] << "\n"
                    << "id = " << id_ << "\n"
                    << "type = " << typeToString(type_) << "\n"
                    << "tree_size = " << tree_size_ << "\n"
                    << "proof_tree_size = " << proof_tree_size_ << "\n"
                    << "solved = " << (solved_ ? "true" : "false") << "\n"
                    << "match_tt = " << (match_tt_ ? "true" : "false") << "\n"
                    << "pruned_by_rzone = " << (pruned_by_rzone_ ? "true" : "false") << "]";
            }
        }
        return oss;
    }

    std::ostringstream& _toSgf(std::ostringstream& oss, bool root) const
    {
        if (root) { oss << "("; }
        if (next_sibling_) {
            if (static_cast<SGFTreeNode*>(next_sibling_)->next_sibling_) {
                oss << "(";
                _toSgfString(oss);
                if (child_) { static_cast<SGFTreeNode*>(child_)->_toSgf(oss, false); }
                oss << ")";
                static_cast<SGFTreeNode*>(next_sibling_)->_toSgf(oss, false);
            } else {
                oss << "(";
                _toSgfString(oss);
                if (child_) { static_cast<SGFTreeNode*>(child_)->_toSgf(oss, false); }
                oss << ")(";
                static_cast<SGFTreeNode*>(next_sibling_)->_toSgf(oss, false) << ")";
            }
        } else {
            _toSgfString(oss);
            if (child_) { static_cast<SGFTreeNode*>(child_)->_toSgf(oss, false); }
        }
        if (root) { oss << ")"; }
        return oss;
    }

public:
    bool match_tt_ = false;
    bool pruned_by_rzone_ = false;

    std::vector<std::pair<std::string, std::vector<std::string>>> properties_;
};
#pragma pack(pop)

template <typename NodeType>
class SGFTreeLoader {
    class LambdaNodeAllocator : public BaseNodeAllocator {
    public:
        LambdaNodeAllocator(std::function<NodeType*()> allocate_func, std::function<void(NodeType*)> deallocate_func)
            : allocate_func_(allocate_func), deallocate_func_(deallocate_func) {}

        BaseSGFNode* allocate() override
        {
            NodeType* node = allocate_func_();
            node->id_ = id_counter_++;
            return node;
        }

        void deallocate(BaseSGFNode* node) override
        {
            deallocate_func_(static_cast<NodeType*>(node));
        }

    private:
        std::function<NodeType*()> allocate_func_;
        std::function<void(NodeType*)> deallocate_func_;
        size_t id_counter_ = 0;
    };

public:
    Tree<NodeType> loadFromString(const std::string& sgf_string)
    {
        StringInputStream input(sgf_string);
        return loadSgf(input);
    }

    Tree<NodeType> loadFromFile(const std::string& sgf_path)
    {
        FileInputStream input(sgf_path);
        return loadSgf(input);
    }

private:
    Tree<NodeType> loadSgf(BaseInputStream& input_stream)
    {
        Tree<NodeType> tree;
        LambdaNodeAllocator allocator(
            [&tree]() -> NodeType* { return tree.createNode(); },
            [&tree](NodeType* node) { tree.deleteNode(node); });
        SGFParser parser(input_stream, allocator);
        NodeType* root = static_cast<NodeType*>(parser.nextNode());
        while (parser.nextNode());
        tree.setRootNode(root);
        dfsTreeSize(tree.getRootNode());
        return tree;
    }

    void dfsTreeSize(NodeType* node)
    {
        if (node->num_children_ == 0) { // leaf node
            node->tree_size_ = 1;
            node->proof_tree_size_ = node->solved_ ? 1 : 0;
            return;
        }
        size_t tree_size = 1;
        // assume the tree already has correct AND/OR structure (TODO: handle match_tt = true issue)
        size_t proof_tree_size = node->type_ == NodeType::Type::AND ? 0 : std::numeric_limits<size_t>::max();
        for (auto child = node->child_; child != nullptr; child = child->next_sibling_) {
            NodeType* child_node = static_cast<NodeType*>(child);
            dfsTreeSize(child_node);
            tree_size += child_node->tree_size_;
            if (node->type_ == NodeType::Type::AND && child_node->solved_) { // sum for AND node
                // assert(node->solved_ == false || child_node->solved_); // assertion failed if match_tt = true
                proof_tree_size += child_node->proof_tree_size_;
            } else if (node->type_ == NodeType::Type::OR && child_node->solved_) { // min for OR node
                proof_tree_size = std::min(proof_tree_size, child_node->proof_tree_size_);
            }
        }
        node->tree_size_ = tree_size;
        // assert(node->solved_ == false || proof_tree_size != std::numeric_limits<size_t>::max()); // assertion failed if match_tt = true
        if (!(node->solved_ == false || proof_tree_size != std::numeric_limits<size_t>::max())) { // hotfix for match_tt = true
            assert(node->solved_ == true);
            node->proof_tree_size_ = 1;
        } else {
            node->proof_tree_size_ = node->solved_ ? proof_tree_size + 1 : 0;
        }
    }
};
