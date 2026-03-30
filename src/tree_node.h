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
        allocated++;
    }
    ~TreeNode()
    {
        for (auto* child : children)
        {
            deallocated++;
            delete child;
        }
    }
    inline static std::atomic<uint64_t> allocated{ 0 };
    inline static std::atomic<uint64_t> deallocated{ 0 };

    void Clear() {
        for (size_t i = 0; i < N; ++i) {
            if (children[i]) {
                delete children[i];    // This triggers the destructor chain
                children[i] = nullptr; // Crucial: prevent dangling pointers
            }
        }
    }
};