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
    TreeNode()
    {
        for (auto& c : children) c = nullptr;
    }
    ~TreeNode()
    {
        for (auto* child : children)
        {
            delete child;
        }
    }

    bool is_leaf()
    {
        for (int i = 0; i < N; i++)
        {
            if (children[i])
                return false;
        }
        return true;
    }

    void cleanup() {
        for (size_t i = 0; i < N; ++i) {
            if (children[i]) {
                delete children[i];
                children[i] = nullptr;
            }
        }
    }
};