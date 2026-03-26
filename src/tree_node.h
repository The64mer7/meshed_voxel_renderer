#pragma once

template<size_t N, typename T>
class TreeNode
{
public:
    T value;
    TreeNode<N, T>* children[N];
    size_t get_child_count()
    {
        return N;
    }
    TreeNode() : children{}
    {
    }
    ~TreeNode()
    {
        for (auto* child : children)
            delete child;
    }
    void Clear() {
        for (size_t i = 0; i < 4; ++i) {
            if (children[i]) {
                delete children[i];    // This triggers the destructor chain
                children[i] = nullptr; // Crucial: prevent dangling pointers
            }
        }
    }
};