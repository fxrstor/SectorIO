#pragma once
#include "new.hpp"
#include <ntifs.h>

template <bool Condition, typename TrueType, typename FalseType>
struct conditional
{
    using type = TrueType;
};

template <typename TrueType, typename FalseType>
struct conditional<false, TrueType, FalseType>
{
    using type = FalseType;
};

template <bool Condition, typename TrueType, typename FalseType>
using conditional_t = typename conditional<Condition, TrueType, FalseType>::type;


template<typename T, bool ThreadSafe = true>
class vector {
private:
    struct Node
    {
        LIST_ENTRY entry;
        T data;
    };

    PLIST_ENTRY _RemoveHead(_Inout_ PLIST_ENTRY head) {
        if (IsListEmpty(head))
            return nullptr;

        return RemoveHeadList(head);
    }

    LIST_ENTRY _head;
    ULONG _count;

    KSPIN_LOCK _lock;

    struct real_scoped_lock {
        KSPIN_LOCK* lock;
        KIRQL oldIrql;
        bool atDpc;

        explicit real_scoped_lock(KSPIN_LOCK* l) : lock(l), oldIrql(0), atDpc(false) {
            if (!lock) return;
            KIRQL curr = KeGetCurrentIrql();
            if (curr < DISPATCH_LEVEL) {
                KeAcquireSpinLock(lock, &oldIrql);
                atDpc = false;
            }
            else {
                KeAcquireSpinLockAtDpcLevel(lock);
                atDpc = true;
            }
        }

        real_scoped_lock(const real_scoped_lock&) = delete;
        real_scoped_lock& operator=(const real_scoped_lock&) = delete;

        real_scoped_lock(real_scoped_lock&& other) noexcept : lock(other.lock), oldIrql(other.oldIrql), atDpc(other.atDpc)
        {
            other.lock = nullptr;
            other.oldIrql = 0;
            other.atDpc = false;
        }

        real_scoped_lock& operator=(real_scoped_lock&& other) noexcept {
            if (this == &other) return *this;
            if (lock) {
                if (atDpc) KeReleaseSpinLockFromDpcLevel(lock);
                else KeReleaseSpinLock(lock, oldIrql);
            }
            lock = other.lock;
            oldIrql = other.oldIrql;
            atDpc = other.atDpc;

            other.lock = nullptr;
            other.oldIrql = 0;
            other.atDpc = false;
            return *this;
        }

        ~real_scoped_lock() {
            if (!lock) return;
            if (atDpc) KeReleaseSpinLockFromDpcLevel(lock);
            else KeReleaseSpinLock(lock, oldIrql);
        }
    };

    struct dummy_scoped_lock {
        explicit dummy_scoped_lock(KSPIN_LOCK*) {}
        dummy_scoped_lock(const dummy_scoped_lock&) = default;
        dummy_scoped_lock& operator=(const dummy_scoped_lock&) = default;
        dummy_scoped_lock(dummy_scoped_lock&&) = default;
        dummy_scoped_lock& operator=(dummy_scoped_lock&&) = default;
        ~dummy_scoped_lock() = default;
    };


    static void _AcquireTwoLocksInOrder(KSPIN_LOCK* first, KSPIN_LOCK* second, KIRQL* firstOldIrql, bool* firstAtDpc) {
        if constexpr (!ThreadSafe) {
            if (firstOldIrql) *firstOldIrql = 0;
            if (firstAtDpc) *firstAtDpc = false;
            return;
        }

        KIRQL curr = KeGetCurrentIrql();
        if (curr < DISPATCH_LEVEL) {
            KeAcquireSpinLock(first, firstOldIrql);
            *firstAtDpc = false;
            KeAcquireSpinLockAtDpcLevel(second);
        }
        else {
            KeAcquireSpinLockAtDpcLevel(first);
            *firstAtDpc = true;
            KeAcquireSpinLockAtDpcLevel(second);
        }
    }

    static void _ReleaseTwoLocksInOrder(KSPIN_LOCK* first, KSPIN_LOCK* second, KIRQL firstOldIrql, bool firstAtDpc) {
        if constexpr (!ThreadSafe) {
            UNREFERENCED_PARAMETER(first);
            UNREFERENCED_PARAMETER(second);
            UNREFERENCED_PARAMETER(firstOldIrql);
            UNREFERENCED_PARAMETER(firstAtDpc);
            return;
        }

        KeReleaseSpinLockFromDpcLevel(second);
        if (firstAtDpc) KeReleaseSpinLockFromDpcLevel(first);
        else KeReleaseSpinLock(first, firstOldIrql);
    }

    using scoped_lock = conditional_t<ThreadSafe, real_scoped_lock, dummy_scoped_lock>;
public:
    struct iterator;
    struct const_iterator;

    vector() : _count(0) {
        InitializeListHead(&_head);
        KeInitializeSpinLock(&_lock);
    }

    ~vector() {
        clear();
    }

    vector(_In_ const vector& other) : _count(0) {
        InitializeListHead(&_head);
        KeInitializeSpinLock(&_lock);
        append(other);
    }

    scoped_lock lock() { 
        return scoped_lock(&_lock);
    }

    NTSTATUS push_back(_In_ const T& item) {
        Node* newNode = new (NON_PAGED) Node;
        if (newNode == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(&newNode->data, &item, sizeof(T));
        InitializeListHead(&newNode->entry);

        {
            scoped_lock lk(&_lock);
            InsertTailList(&_head, &newNode->entry);
            _count++;
        }
        return STATUS_SUCCESS;
    }

    BOOLEAN pop_back(_Out_ T* other) {
        PLIST_ENTRY tail = nullptr;
        Node* n = nullptr;

        {
            scoped_lock lk(&_lock);
            if (is_empty()) {
                return FALSE;
            }
            tail = _head.Blink;
            RemoveEntryList(tail);
            _count--;

            n = CONTAINING_RECORD(tail, Node, entry);
        }

        RtlCopyMemory(other, &n->data, sizeof(T));
        delete n;
        return TRUE;
    }

    BOOLEAN pop_back()
    {
        PLIST_ENTRY tail = nullptr;
        Node* n = nullptr;

        {
            scoped_lock lk(&_lock);
            if (is_empty())
                return FALSE;

            tail = _head.Blink;
            RemoveEntryList(tail);
            _count--;
            n = CONTAINING_RECORD(tail, Node, entry);
        }

        delete n;
        return TRUE;
    }

    NTSTATUS append(_In_ const vector& src) {
        KSPIN_LOCK* firstLock = (&_lock < &src._lock) ? &_lock : const_cast<KSPIN_LOCK*>(&src._lock);
        KSPIN_LOCK* secondLock = (firstLock == &_lock) ? const_cast<KSPIN_LOCK*>(&src._lock) : &_lock;

        KIRQL firstOldIrql = 0;
        bool firstAtDpc = false;
        _AcquireTwoLocksInOrder(firstLock, secondLock, &firstOldIrql, &firstAtDpc);

        PLIST_ENTRY it = src._head.Flink;
        NTSTATUS status = STATUS_SUCCESS;
        for (; it != &src._head; it = it->Flink) {
            const Node* n = CONTAINING_RECORD(it, Node, entry);
            Node* newNode = new (NON_PAGED) Node;
            if (!newNode) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            RtlCopyMemory(&newNode->data, &n->data, sizeof(T));
            InsertTailList(&_head, &newNode->entry);
            _count++;
        }

        _ReleaseTwoLocksInOrder(firstLock, secondLock, firstOldIrql, firstAtDpc);
        return status;
    }

    T* front() {
        if (is_empty())
            return nullptr;

        PLIST_ENTRY first = _head.Flink;
        Node* n = CONTAINING_RECORD(first, Node, entry);
        return &n->data;
    }

    T* back() {
        if (is_empty())
            return nullptr;

        PLIST_ENTRY last = _head.Blink;
        Node* n = CONTAINING_RECORD(last, Node, entry);
        return &n->data;
    }

    T* data() const {
        ULONG count = size();
        if (count == 0) return nullptr;

        T* array = new (NON_PAGED) T[count];
        ULONG i = 0;
        for (auto& elem : locked()) {
            RtlCopyMemory(&array[i++], &elem, sizeof(T));
        }
        return array;
    }

    NTSTATUS insert(_In_ ULONG index, _In_ const T& item) {
        Node* newNode = new (NON_PAGED) Node;
        if (newNode == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(&newNode->data, &item, sizeof(T));

        {
            scoped_lock lk(&_lock);
            if (index >= _count) {
                InsertTailList(&_head, &newNode->entry);
            }
            else {
                PLIST_ENTRY ptr = _head.Flink;
                for (ULONG i = 0; i < index; i++)
                    ptr = ptr->Flink;
                InsertTailList(ptr->Blink, &newNode->entry);
            }
            _count++;
        }
        return STATUS_SUCCESS;
    }

    BOOLEAN remove(_In_ ULONG index) {
        PLIST_ENTRY target = nullptr;
        Node* n = nullptr;

        {
            scoped_lock lk(&_lock);
            if (index >= _count)
                return FALSE;

            PLIST_ENTRY ptr = _head.Flink;
            for (ULONG i = 0; i < index; i++)
                ptr = ptr->Flink;

            RemoveEntryList(ptr);
            _count--;
            target = ptr;
            n = CONTAINING_RECORD(ptr, Node, entry);
        }

        delete n;
        return TRUE;
    }

    void invert() {
        scoped_lock lk(&_lock);
        if (_count <= 1)
            return;

        PLIST_ENTRY current = _head.Flink;
        for (ULONG i = 0; i < _count; i++) {
            PLIST_ENTRY next = current->Flink;
            PLIST_ENTRY tmp = current->Flink;
            current->Flink = current->Blink;
            current->Blink = tmp;
            current = next;
        }

        PLIST_ENTRY tmp_head = _head.Flink;
        _head.Flink = _head.Blink;
        _head.Blink = tmp_head;
    }

    T* find(_In_ const T& item) {
        scoped_lock lk(&_lock);
        PLIST_ENTRY ptr = _head.Flink;
        while (ptr != &_head) {
            Node* n = CONTAINING_RECORD(ptr, Node, entry);
            if (n->data == item) {
                return &n->data;
            }
            ptr = ptr->Flink;
        }
        return nullptr;
    }

    T* at(_In_ ULONG index) {
        scoped_lock lk(&_lock);
        if (index >= _count)
            return nullptr;

        PLIST_ENTRY ptr = _head.Flink;
        for (ULONG i = 0; i < index; i++)
            ptr = ptr->Flink;

        Node* n = CONTAINING_RECORD(ptr, Node, entry);
        return &n->data;
    }

    T* get(_In_ ULONG index) {
        return at(index);
    }

    iterator erase(iterator it) {
        if (it.curr == &_head)
            return end();

        PLIST_ENTRY next = nullptr;
        Node* n = nullptr;

        {
            scoped_lock lk(&_lock);
            if (it.curr == &_head) return end();
            next = it.curr->Flink;
            n = CONTAINING_RECORD(it.curr, Node, entry);
            RemoveEntryList(it.curr);
            _count--;
        }

        delete n;
        return iterator(next, &_head);
    }

    T* operator[](_In_ ULONG index) {
        return at(index);
    }

    vector& operator+=(_In_ const T& item) {
        (void)push_back(item);
        return *this;
    }

    vector& operator+=(_In_ const vector& other) {
        append(other);
        return *this;
    }

    vector& operator--() {
        T discard;
        (void)pop_back(&discard);
        return *this;
    }

    vector& operator=(_In_ const vector& other) {
        if (this != &other) {
            clear();
            append(other);
        }
        return *this;
    }

    BOOLEAN operator==(_In_ const vector& other) const {
        KSPIN_LOCK* firstLock = (&_lock < &other._lock) ? const_cast<KSPIN_LOCK*>(&_lock) : const_cast<KSPIN_LOCK*>(&other._lock);
        KSPIN_LOCK* secondLock = (firstLock == &_lock) ? const_cast<KSPIN_LOCK*>(&other._lock) : const_cast<KSPIN_LOCK*>(&_lock);

        KIRQL firstOldIrql = 0;
        bool firstAtDpc = false;
        _AcquireTwoLocksInOrder(firstLock, secondLock, &firstOldIrql, &firstAtDpc);

        BOOLEAN equal = TRUE;
        if (_count != other._count) {
            equal = FALSE;
        }
        else {
            PLIST_ENTRY p1 = _head.Flink;
            PLIST_ENTRY p2 = other._head.Flink;
            while (p1 != &_head && p2 != &other._head) {
                const Node* n1 = CONTAINING_RECORD(p1, Node, entry);
                const Node* n2 = CONTAINING_RECORD(p2, Node, entry);
                if (!(n1->data == n2->data)) {
                    equal = FALSE;
                    break;
                }
                p1 = p1->Flink;
                p2 = p2->Flink;
            }
        }

        _ReleaseTwoLocksInOrder(firstLock, secondLock, firstOldIrql, firstAtDpc);
        return equal;
    }

    BOOLEAN operator!=(_In_ const vector& other) const {
        bool equal = *this == other;
        return !equal;
    }

    ULONG size() const {
        if constexpr (!ThreadSafe) {
            return _count;
        }

        KIRQL old;
        KSPIN_LOCK* lockPtr = const_cast<KSPIN_LOCK*>(&_lock);
        KIRQL curr = KeGetCurrentIrql();
        if (curr < DISPATCH_LEVEL) {
            KeAcquireSpinLock(lockPtr, &old);
            ULONG s = _count;
            KeReleaseSpinLock(lockPtr, old);
            return s;
        }
        else {
            KeAcquireSpinLockAtDpcLevel(lockPtr);
            ULONG s = _count;
            KeReleaseSpinLockFromDpcLevel(lockPtr);
            return s;
        }
    }

    BOOLEAN is_empty() const {
        return (size() == 0);
    }

    void clear() {
        PLIST_ENTRY entry;
        {
            scoped_lock lk(&_lock);
            while ((entry = _RemoveHead(&_head)) != nullptr) {
                Node* n = CONTAINING_RECORD(entry, Node, entry);
                delete n;
            }
            _count = 0;
            InitializeListHead(&_head);
        }
    }

    struct iterator
    {
        PLIST_ENTRY curr;
        PLIST_ENTRY headSentinel;

        iterator(_In_ PLIST_ENTRY start, _In_ PLIST_ENTRY head_ptr) : curr(start), headSentinel(head_ptr) { }

        T& operator*() const {
            Node* n = CONTAINING_RECORD(curr, Node, entry);
            return n->data;
        }

        T* operator->() const {
            Node* n = CONTAINING_RECORD(curr, Node, entry);
            return &n->data;
        }

        iterator& operator++() {
            curr = curr->Flink;
            return *this;
        }

        BOOLEAN operator!=(const iterator& other) const {
            return (curr != other.curr);
        }
    };

    struct const_iterator
    {
        PLIST_ENTRY curr;
        PLIST_ENTRY headSentinel;

        const_iterator(_In_ PLIST_ENTRY start, _In_ PLIST_ENTRY head_ptr) : curr(start), headSentinel(head_ptr) { }

        const T& operator*() const {
            const Node* n = CONTAINING_RECORD(curr, Node, entry);
            return n->data;
        }

        const T* operator->() const {
            const Node* n = CONTAINING_RECORD(curr, Node, entry);
            return &n->data;
        }

        const_iterator& operator++() {
            curr = curr->Flink;
            return *this;
        }

        BOOLEAN operator!=(const const_iterator& other) const {
            return (curr != other.curr);
        }
    };

    iterator begin() {
        return iterator(_head.Flink, &_head);
    }

    iterator end() {
        return iterator(&_head, &_head);
    }

    const_iterator cbegin() const {
        return const_iterator(_head.Flink, const_cast<PLIST_ENTRY>(&_head));
    }
    const_iterator cend() const {
        return const_iterator(const_cast<PLIST_ENTRY>(&_head), const_cast<PLIST_ENTRY>(&_head));
    }

    struct locked_range {
        scoped_lock guard;
        iterator b;
        iterator e;

        explicit locked_range(vector& v)
            : guard(&v._lock), b(v._head.Flink, &v._head), e(&v._head, &v._head) {}

        locked_range(const locked_range&) = delete;
        locked_range& operator=(const locked_range&) = delete;
        locked_range(locked_range&&) = default;
        locked_range& operator=(locked_range&&) = default;

        iterator begin() { return b; }
        iterator end() { return e; }
    };

    struct const_locked_range {
        scoped_lock guard;
        const_iterator b;
        const_iterator e;

        explicit const_locked_range(const vector& v)
            : guard(const_cast<KSPIN_LOCK*>(&v._lock)),
            b(v._head.Flink, const_cast<PLIST_ENTRY>(&v._head)),
            e(const_cast<PLIST_ENTRY>(&v._head), const_cast<PLIST_ENTRY>(&v._head)) {}

        const_locked_range(const const_locked_range&) = delete;
        const_locked_range& operator=(const const_locked_range&) = delete;
        const_locked_range(const_locked_range&&) = default;
        const_locked_range& operator=(const_locked_range&&) = default;

        const_iterator begin() { return b; }
        const_iterator end() { return e; }
    };

    locked_range locked() { return locked_range(*this); }
    const_locked_range locked() const { return const_locked_range(*this); }
};
