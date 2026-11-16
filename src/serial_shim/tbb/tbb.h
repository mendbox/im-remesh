// SPDX-License-Identifier: GPL-3.0-or-later
// Serial stand-in for the small part of Intel TBB that the Instant Meshes core uses.
// Written from the public TBB interface names only; no TBB code is included or needed.
// Everything runs on the calling thread, in index order, so results repeat exactly.
// Nothing here leaves an order to the C++ standard library (libc++, libstdc++ and the MSVC STL order equal keys
// differently in std::sort and std::priority_queue), so that results can be the same on every platform.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>   // the core calls _InterlockedCompareExchange; the real TBB headers used to bring this in
#endif

namespace tbb {

template <typename T> class blocked_range {
public:
    typedef T const_iterator;
    blocked_range(T begin, T end, std::size_t grainsize = 1)
        : m_begin(begin), m_end(end), m_grain(grainsize) {}
    T begin() const { return m_begin; }
    T end() const { return m_end; }
    std::size_t size() const { return (std::size_t) (m_end - m_begin); }
    std::size_t grainsize() const { return m_grain; }
    bool empty() const { return !(m_begin < m_end); }
    bool is_divisible() const { return m_grain < size(); }
private:
    T m_begin, m_end;
    std::size_t m_grain;
};

template <typename Range, typename Body>
void parallel_for(const Range &range, const Body &body) {
    if (!range.empty())
        body(range);
}

template <typename Index, typename Function>
void parallel_for(Index first, Index last, const Function &function) {
    for (Index i = first; i < last; ++i)
        function(i);
}

template <typename Range, typename Value, typename Func, typename Reduction>
Value parallel_reduce(const Range &range, const Value &identity, const Func &func, const Reduction &) {
    if (range.empty())
        return identity;
    return func(range, identity);
}

template <typename Range, typename Value, typename Func, typename Reduction>
Value parallel_deterministic_reduce(const Range &range, const Value &identity, const Func &func, const Reduction &reduction) {
    return parallel_reduce(range, identity, func, reduction);
}

// Stable on purpose: with equal keys std::sort leaves an order that differs between standard libraries.
template <typename Iterator, typename Compare>
void parallel_sort(Iterator begin, Iterator end, const Compare &comp) {
    std::stable_sort(begin, end, comp);
}

template <typename Iterator> void parallel_sort(Iterator begin, Iterator end) {
    std::stable_sort(begin, end);
}

class spin_mutex {
public:
    class scoped_lock {
    public:
        scoped_lock() {}
        explicit scoped_lock(spin_mutex &) {}
        void acquire(spin_mutex &) {}
        bool try_acquire(spin_mutex &) { return true; }
        void release() {}
    };
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
};

template <typename T> class concurrent_vector : public std::vector<T> {
public:
    using std::vector<T>::vector;
};

// Max-heap with a fully defined order: the largest element under Compare comes out first, and among elements that
// Compare calls equal the one pushed first (lowest push index) comes out first. std::priority_queue is not used
// because its order for equal keys depends on the heap code of the standard library.
template <typename T, typename Compare = std::less<T> > class concurrent_priority_queue {
public:
    concurrent_priority_queue() : m_next(0) {}
    explicit concurrent_priority_queue(const Compare &comp) : m_comp(comp), m_next(0) {}
    void push(const T &value) {
        m_heap.push_back(Item(value, m_next++));
        std::size_t i = m_heap.size() - 1;
        while (i > 0) {                                   // sift up
            std::size_t parent = (i - 1) / 2;
            if (!below(m_heap[parent], m_heap[i]))
                break;
            std::swap(m_heap[parent], m_heap[i]);
            i = parent;
        }
    }
    bool try_pop(T &value) {
        if (m_heap.empty())
            return false;
        value = m_heap.front().value;
        m_heap.front() = m_heap.back();
        m_heap.pop_back();
        const std::size_t n = m_heap.size();
        std::size_t i = 0;
        while (true) {                                    // sift down
            std::size_t left = 2 * i + 1, right = left + 1, top = i;
            if (left < n && below(m_heap[top], m_heap[left]))
                top = left;
            if (right < n && below(m_heap[top], m_heap[right]))
                top = right;
            if (top == i)
                break;
            std::swap(m_heap[i], m_heap[top]);
            i = top;
        }
        return true;
    }
    bool empty() const { return m_heap.empty(); }
    std::size_t size() const { return m_heap.size(); }
private:
    struct Item {
        T value;
        unsigned long long index;                         // push index: the tie-break
        Item(const T &value, unsigned long long index) : value(value), index(index) {}
    };
    // True when a has to leave the queue after b. A strict total order: no two items compare equal.
    bool below(const Item &a, const Item &b) const {
        if (m_comp(a.value, b.value))
            return true;
        if (m_comp(b.value, a.value))
            return false;
        return a.index > b.index;
    }
    std::vector<Item> m_heap;
    Compare m_comp;
    unsigned long long m_next;
};

class task_scheduler_init {
public:
    static const int automatic = -1;
    explicit task_scheduler_init(int = automatic) {}
    static int default_num_threads() { return 1; }
};

// Old-style tbb::task, only as far as the BVH builder needs it: a task splits its range, hands one half to
// spawn() and continues with the other half by returning itself. Here spawned tasks wait on a stack and run
// one after another. The BVH node indices are fixed before the split, so the order does not change the result.
class task {
public:
    struct placement {};
    virtual ~task() {}
    virtual task *execute() = 0;

    static placement allocate_root() { return placement(); }
    placement allocate_continuation() { return placement(); }
    placement allocate_child() { return placement(); }
    void set_ref_count(int) {}
    void recycle_as_child_of(task &) { m_recycled = true; }
    void spawn(task &t) { pending().push_back(&t); }

    static void spawn_root_and_wait(task &root) {
        std::vector<task *> &stack = pending();
        std::vector<task *> &idle = continuations();
        task *t = &root;
        try {
            while (t) {
                while (t) {
                    t->m_recycled = false;
                    task *next = t->execute();
                    if (!t->m_recycled || next != t)
                        release(t);
                    t = next;
                }
                if (!stack.empty()) {
                    t = stack.back();
                    stack.pop_back();
                }
            }
        } catch (...) {
            // execute() threw (in practice: out of memory). Nothing may stay queued for the next call on this thread.
            if (t)
                release(t);
            for (std::size_t i = 0; i < stack.size(); ++i)
                release(stack[i]);
            stack.clear();
            for (std::size_t i = 0; i < idle.size(); ++i)
                release(idle[i]);
            idle.clear();
            throw;
        }
        for (std::size_t i = 0; i < idle.size(); ++i)
            release(idle[i]);
        idle.clear();
    }

    static std::vector<task *> &pending() { static thread_local std::vector<task *> s; return s; }
    static std::vector<task *> &continuations() { static thread_local std::vector<task *> s; return s; }

private:
    static void release(task *t) { t->~task(); ::operator delete(static_cast<void *>(t)); }
    bool m_recycled = false;
};

class empty_task : public task {
public:
    empty_task() { continuations().push_back(this); }
    task *execute() { return nullptr; }
};

} // namespace tbb

inline void *operator new(std::size_t size, const tbb::task::placement &) { return ::operator new(size); }
inline void operator delete(void *p, const tbb::task::placement &) { ::operator delete(p); }
