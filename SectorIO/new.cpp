#include "new.hpp"

void* __cdecl operator new(size_t size, POOL_T pool, unsigned long tag) {
	return AllocateMemory(pool, size, tag);
}

void* __cdecl operator new[](size_t size, POOL_T pool, unsigned long tag) {
	return AllocateMemory(pool, size, tag);
}

inline void* operator new(size_t, void* where) {
	return where;
}

void __cdecl operator delete(void* ptr, size_t) {
	FreePtr(ptr);
}

void __cdecl operator delete(void* ptr) {
	FreePtr(ptr);
}

void __cdecl operator delete[](void* ptr, size_t) {
	FreePtr(ptr);
}

void __cdecl operator delete[](void* ptr) {
	FreePtr(ptr);
}

template <typename T>
typename remove_reference<T>::type&& move(T&& arg) {
	return static_cast<typename remove_reference<T>::type&&>(arg);
}

template<class _Ty> inline
constexpr _Ty&& forward(typename remove_reference<_Ty>::type& _Arg) {
	return (static_cast<_Ty&&>(_Arg));
}

template<class _Ty> inline
constexpr _Ty&& forward(typename remove_reference<_Ty>::type&& _Arg) {
	return (static_cast<_Ty&&>(_Arg));
}