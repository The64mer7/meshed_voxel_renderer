#pragma once

#include <iostream>
#include <stack>
#include <cstddef>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

class VirtualMemory
{
public:
    static void* reserve(size_t size) {
#ifdef _WIN32
        return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
        void* ptr = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (ptr == MAP_FAILED) ? nullptr : ptr;
#endif
    }

    static bool commit(void* address, size_t size) {
#ifdef _WIN32
        return VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
        return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
#endif
    }

    static void decommit(void* address, size_t size) {
#ifdef _WIN32
        VirtualFree(address, size, MEM_DECOMMIT);
#else
        mmap(address, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    }

    static void free(void* address, size_t size) {
#ifdef _WIN32
        VirtualFree(address, 0, MEM_RELEASE);
#else
        munmap(address, size);
#endif
    }
};

class ArenaAllocator
{
public:
    void init(uint64_t initial_capacity)
    {
        m_memory = (uint8_t*)m_virtual_memory.reserve(1024ull * 1024ull * 1024ull);
        resize(initial_capacity);
    }

    int32_t resize(uint64_t capacity_needed)
    {
        if (capacity_needed <= m_capacity)
            return 0;

        if (capacity_needed < m_top)
            return 1;

        uint64_t capacity_diff = ((capacity_needed - m_capacity + 4096 - 1) / 4096) * 4096;
        if (!m_virtual_memory.commit(m_memory + m_capacity, capacity_diff))
            return 1;

        m_capacity += capacity_diff;

        return 0;
    }

    template<typename T>
    inline T* allocate(uint64_t count = 1)
    {
        return static_cast<T*>(allocate_impl(sizeof(T) * count, alignof(T)));
    }

    inline void* allocate_generic(uint64_t size, uint64_t align)
    {
        return allocate_impl(size, align);
    }

    template<typename T, typename... Args>
    inline T* allocate_construct(Args&&... args)
    {
        T* ptr = static_cast<T*>(allocate_impl(sizeof(T), alignof(T)));
        if (ptr == nullptr)
            return nullptr;

        return new (ptr) T(std::forward<Args>(args)...);
    }

    void reset()
    {
        m_top = 0;
    }

    void shutdown()
    {
        if (m_memory)
        {
            m_virtual_memory.free(m_memory, 4 * 1024 * 1024);
            m_memory = nullptr;
            m_capacity = 0;
            m_top = 0;
        }
    }

    void decommit_all() {
        if (m_memory && m_capacity > 0) {
            m_virtual_memory.decommit(m_memory, m_capacity);
            m_capacity = 0;
            m_top = 0;
        }
    }

    uint64_t get_commited_size() const
    {
        return m_capacity;
    }

    uint64_t get_used_size() const
    {
        return m_top;
    }

private:
    void* allocate_impl(uint64_t size, uint64_t align)
    {
        uint64_t aligned = (m_top + align - 1) & ~(align - 1);
        uint64_t needed_top = aligned + size;

        if (needed_top > m_capacity)
        {
            uint64_t exp_capacity = m_capacity + (m_capacity >> 1);

            uint64_t new_capacity = exp_capacity > needed_top ? exp_capacity : needed_top;
            if (resize(new_capacity) != 0)
                return nullptr;
        }
        m_top = needed_top;
        return m_memory + aligned;
    }

    VirtualMemory m_virtual_memory;
    uint8_t* m_memory = nullptr;
    uint64_t m_top = 0;
    uint64_t m_capacity = 0;
};

class PoolAllocator
{
public:
    template<typename T>
    void init(uint64_t initial_objects_capacity)
    {
        m_allocation_size = sizeof(T);
        m_alignment = alignof(T);
        m_allocator.init(initial_objects_capacity * sizeof(T));
    }

    void* allocate()
    {
        if (!m_free_memory.empty())
        {
            void* ptr = m_free_memory.top();
            m_free_memory.pop();
            return ptr;
        }

        return m_allocator.allocate_generic(m_allocation_size, m_alignment);
    }

    void deallocate(void* ptr)
    {
        m_free_memory.push(ptr);
    }

    void shutdown()
    {
        m_allocator.shutdown();
    }
private:
    uint64_t m_allocation_size = 0;
    uint64_t m_alignment = 0;
    std::stack<void*> m_free_memory;
    ArenaAllocator m_allocator;
};