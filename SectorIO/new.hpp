#pragma once
#include <ntddk.h>

#ifndef DRIVER_TAG
#define DRIVER_TAG ' wen'
#endif

#ifdef NO_DEPRECATED_FUNCTIONS
#define POOL_T POOL_FLAGS

#define AllocateMemory(pool, size, tag) ExAllocatePool2(pool, size, tag);
#define FreePtr(ptr) ExFreePool2(ptr, DRIVER_TAG, NULL, 0)

#define NON_PAGED POOL_FLAG_NON_PAGED
#define PAGED_POOL POOL_FLAG_PAGED
#else
#define POOL_T POOL_TYPE

#define AllocateMemory(pool, size, tag) ExAllocatePoolWithTag(pool, size, tag);
#define FreePtr(ptr) ExFreePool(ptr)

#define NON_PAGED NonPagedPoolNx
#define PAGED_POOL PagedPool
#endif

void* __cdecl operator new(size_t size, POOL_T pool, unsigned long tag = DRIVER_TAG);
void* __cdecl operator new[](size_t size, POOL_T pool, unsigned long tag = DRIVER_TAG);
inline void* operator new(size_t, void* where);

void __cdecl operator delete(void* ptr, size_t);
void __cdecl operator delete(void* ptr);
void __cdecl operator delete[](void* ptr, size_t);
void __cdecl operator delete[](void* ptr);

template<class _Ty>
struct remove_reference
{
	typedef _Ty type;
};

template<class _Ty>
struct remove_reference<_Ty&>
{
	typedef _Ty type;
};

template<class _Ty>
struct remove_reference<_Ty&&>
{
	typedef _Ty type;
};

template<class T>
using remove_reference_t = typename remove_reference<T>::type;

template <typename T>
typename remove_reference<T>::type&& move(T&& arg);

template<class _Ty> inline
constexpr _Ty&& forward(typename remove_reference<_Ty>::type& _Arg);

template<class _Ty> inline
constexpr _Ty&& forward(typename remove_reference<_Ty>::type&& _Arg);