#pragma once
#include "sgf_parser.hpp"
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

class SGFTreeLoader {
public:
#pragma pack(push, 1)
    class SGFTreeNode : public BaseSGFNode {
    public:
        enum class NodeType : int8_t {
            NONE = -1,
            AND,
            OR,
        };

        static std::string nodeTypeToString(NodeType type)
        {
            switch (type) {
                case NodeType::AND:
                    return "AND";
                case NodeType::OR:
                    return "OR";
                default:
                    return "NONE";
            }
        }

    public:
        void addProperty(const std::string& tag, const std::vector<std::string>& values) override
        {
            if (tag == "B") {
                assert(values.size() == 1);
                type_ = NodeType::AND;
            }
            if (tag == "W") {
                assert(values.size() == 1);
                type_ = NodeType::OR;
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
            }
        }

        std::string toString() const
        {
            std::ostringstream oss;
            oss << "SGFTreeNode("
                << "id=" << id_ << ", "
                << "type=" << nodeTypeToString(type_) << ", "
                << "tree_size=" << tree_size_ << ", "
                << "proof_tree_size=" << proof_tree_size_ << ", "
                << "solved=" << (solved_ ? "true" : "false")
                << ")";
            return oss.str();
        }

    public:
        size_t id_ = 0;
        NodeType type_ = NodeType::NONE;
        size_t tree_size_ = 0;
        size_t proof_tree_size_ = 0;
        bool solved_ = false;
    };
#pragma pack(pop)

private:
    class NodeAllocator : public TrackingNodeAllocator<SGFTreeNode> {
    public:
        BaseSGFNode* allocate() override
        {
            SGFTreeNode* node = static_cast<SGFTreeNode*>(TrackingNodeAllocator<SGFTreeNode>::allocate());
            node->id_ = id_counter_++;
            nodes_.push_back(node);
            return node;
        }

        size_t id_counter_ = 0;
        std::vector<SGFTreeNode*> nodes_;
    };

public:
    SGFTreeLoader()
    {
        reset();
    }

    ~SGFTreeLoader()
    {
        reset(); // free memory
    }

    void reset()
    {
        for (SGFTreeNode* node : nodes_) {
            delete node;
        }
        nodes_.clear();
    }

    void loadFromString(const std::string& sgf_string)
    {
        StringInputStream input(sgf_string);
        loadSgf(input);
    }

    void loadFromFile(const std::string& sgf_path)
    {
        FileInputStream input(sgf_path);
        loadSgf(input);
    }

    std::vector<SGFTreeNode*>& getNodes() { return nodes_; }
    SGFTreeNode* getRoot() { return nodes_.empty() ? nullptr : nodes_.front(); }
    size_t getTreeSize() { return nodes_.size(); }

private:
    void loadSgf(BaseInputStream& input_stream)
    {
        reset();
        NodeAllocator allocator;
        SGFParser parser(input_stream, allocator);
        while (parser.nextNode());
        nodes_ = std::move(allocator.nodes_);
    }

private:
    std::vector<SGFTreeNode*> nodes_;
};
