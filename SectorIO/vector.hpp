#pragma once
#include "new.hpp"

template<typename T>
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
public:
    vector() : _count(0) {
        InitializeListHead(&_head);
    }

    ~vector() {
        clear();
    }

    vector(_In_ const vector& other) : _count(0) {
        InitializeListHead(&_head);
        append(other);
    }

    NTSTATUS push_back(_In_ const T& item) {
        Node* newNode = new(NON_PAGED) Node;
        if (newNode == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(&newNode->data, &item, sizeof(T));

        InsertTailList(&_head, &newNode->entry);
        _count++;
        return STATUS_SUCCESS;
    }

    BOOLEAN pop_back(_Out_ T* other) {
        if (is_empty()) {
            return FALSE;
        }
        PLIST_ENTRY tail = _head.Blink;
        RemoveEntryList(tail);

        Node* n = CONTAINING_RECORD(tail, Node, entry);
        RtlCopyMemory(other, &n->data, sizeof(T));
        delete[] n;

        _count--;
        return TRUE;
    }

    BOOLEAN pop_back()
    {
        if (is_empty())
            return FALSE;

        PLIST_ENTRY tail = _head.Blink;
        RemoveEntryList(tail);

        Node* n = CONTAINING_RECORD(tail, Node, entry);
        delete[] n;

        _count--;
        return TRUE;
    }

    NTSTATUS append(_In_ const vector& src)
    {
        PLIST_ENTRY it = src._head.Flink;
        while (it != &src._head) {
            const Node* n = CONTAINING_RECORD(it, Node, entry);

            NTSTATUS status = push_back(n->data);
            if (!NT_SUCCESS(status))
                return status;

            it = it->Flink;
        }
        return STATUS_SUCCESS;
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

    NTSTATUS insert(_In_ ULONG index, _In_ const T& item) {
        if (index >= _count)
            return push_back(item);

        Node* newNode = new(NON_PAGED) Node;
        if (newNode == nullptr)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(&newNode->data, &item, sizeof(T));

        PLIST_ENTRY ptr = _head.Flink;
        for (ULONG i = 0; i < index; i++)
            ptr = ptr->Flink;

        InsertTailList(ptr, &newNode->entry);
        _count++;
        return STATUS_SUCCESS;
    }

    BOOLEAN remove(_In_ ULONG index) {
        if (index >= _count)
            return FALSE;

        PLIST_ENTRY ptr = _head.Flink;
        for (ULONG i = 0; i < index; i++)
            ptr = ptr->Flink;

        RemoveEntryList(ptr);
        Node* n = CONTAINING_RECORD(ptr, Node, entry);
        delete[] n;

        _count--;
        return TRUE;
    }

    void invert() {
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
        if (_count != other._count)
            return FALSE;

        PLIST_ENTRY p1 = _head.Flink;
        PLIST_ENTRY p2 = other._head.Flink;
        while (p1 != &_head && p2 != &other._head) {
            const Node* n1 = CONTAINING_RECORD(p1, Node, entry);
            const Node* n2 = CONTAINING_RECORD(p2, Node, entry);
            if (!(n1->data == n2->data))
                return FALSE;

            p1 = p1->Flink;
            p2 = p2->Flink;
        }
        return TRUE;
    }

    BOOLEAN operator!=(_In_ const vector& other) const {
        bool equal = *this == other;
        return !equal;
    }

    ULONG size() const {
        return _count;
    }

    BOOLEAN is_empty() const {
        return (_count == 0);
    }

    void clear() {
        PLIST_ENTRY entry;
        while ((entry = _RemoveHead(&_head)) != nullptr) {
            Node* n = CONTAINING_RECORD(entry, Node, entry);
            delete[] n;
        }

        _count = 0;
        InitializeListHead(&_head);
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
};