#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>

#pragma pack(push, 1)
class BaseTreeNode {
public:
    virtual ~BaseTreeNode() = default;

    virtual void addChild(BaseTreeNode* node)
    {
        node->detach();
        if (child_ == nullptr) {
            child_ = node;
        } else {
            BaseTreeNode* current = child_;
            while (current->next_sibling_ != nullptr) {
                current = current->next_sibling_;
            }
            current->next_sibling_ = node;
        }
        node->parent_ = this;
        ++num_children_;
    }

    /**
     * @brief Detach the node from its parent.
     *
     * @return BaseTreeNode* The detached node.
     */
    virtual BaseTreeNode* detach()
    {
        if (parent_ != nullptr) {
            if (parent_->child_ == this) {
                parent_->child_ = next_sibling_;
            } else {
                BaseTreeNode* ptr = parent_->child_;
                while (ptr->next_sibling_ != this) {
                    ptr = ptr->next_sibling_;
                }
                ptr->next_sibling_ = next_sibling_;
            }
            --parent_->num_children_;
            parent_ = nullptr;
            next_sibling_ = nullptr;
        }
        return this;
    }

public:
    BaseTreeNode* parent_ = nullptr;
    BaseTreeNode* child_ = nullptr;
    BaseTreeNode* next_sibling_ = nullptr;
    size_t num_children_ = 0;
};

class TreeNode : public BaseTreeNode {
public:
    enum class Type : int8_t {
        NONE = -1,
        AND,
        OR,
    };

    static std::string typeToString(Type type)
    {
        switch (type) {
            case Type::AND:
                return "AND";
            case Type::OR:
                return "OR";
            default:
                return "NONE";
        }
    }

public:
    virtual std::string toString() const
    {
        std::ostringstream oss;
        oss << "TreeNode("
            << "id=" << id_ << ", "
            << "type=" << typeToString(type_) << ", "
            << "tree_size=" << tree_size_ << ", "
            << "proof_tree_size=" << proof_tree_size_ << ", "
            << "solved=" << (solved_ ? "true" : "false")
            << ")";
        return oss.str();
    }

public:
    size_t id_ = 0;
    Type type_ = Type::NONE;
    size_t tree_size_ = 0;
    size_t proof_tree_size_ = 0;
    bool solved_ = false;
};
#pragma pack(pop)

template <typename NodeType, typename Allocator = std::allocator<NodeType>>
class Tree {
public:
    using allocator_type = typename Allocator::template rebind<NodeType>::other;

public:
    // copy
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;

    // move
    Tree(Tree&& other)
    {
        allocator_ = std::move(other.allocator_);
        nodes_ = std::move(other.nodes_);
        root_ = other.root_;
        other.root_ = nullptr;
    }
    Tree& operator=(Tree&& other)
    {
        if (this != &other) {
            reset();
            allocator_ = std::move(other.allocator_);
            nodes_ = std::move(other.nodes_);
            root_ = other.root_;
            other.root_ = nullptr;
        }
        return *this;
    }

    Tree()
    {
        reset();
    }

    ~Tree()
    {
        reset(); // free memory
    }

    void reset()
    {
        while (!nodes_.empty()) {
            deleteNode(*nodes_.begin());
        }
        root_ = nullptr;
    }

    template <typename... Args>
    NodeType* createNode(Args&&... args)
    {
        NodeType* node = allocator_.allocate(1);
        try {
            allocator_.construct(node, std::forward<Args>(args)...);
            nodes_.insert(node);
        } catch (...) {
            allocator_.deallocate(node, 1);
            throw;
        }
        return node;
    }

    void deleteNode(NodeType* node)
    {
        nodes_.erase(node);
        allocator_.destroy(node);
        allocator_.deallocate(node, 1);
    }

    void setRootNode(NodeType* node)
    {
        root_ = node;
    }

    std::unordered_set<NodeType*>& getNodes() { return nodes_; }
    NodeType* getRootNode() { return root_; }
    size_t getTreeSize() { return nodes_.size(); }

protected:
    allocator_type allocator_;
    std::unordered_set<NodeType*> nodes_;
    NodeType* root_;
};
