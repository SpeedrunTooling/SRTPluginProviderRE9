#ifndef SRTPLUGINRE9_PROTECTED_PTR_H
#define SRTPLUGINRE9_PROTECTED_PTR_H

#include "Globals.h"
#include <cstdio>
#include <iostream>
#include <print>
#include <type_traits>
#include <windows.h>

inline void protectedLog(const char *message) noexcept
{
	__try
	{
		std::cerr << message << std::endl;
		if (g_logFile != nullptr)
		{
			std::print(g_logFile, "{}\n", message);
			std::fflush(g_logFile);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// Even logging failed — silently swallow.
	}
}

template <typename T>
class Protected_Ptr
{
public:
	Protected_Ptr() noexcept : address(nullptr) {}
	explicit Protected_Ptr(T *address) noexcept : address(address) {}

	explicit operator bool() const noexcept { return address != nullptr; }
	T *get() const noexcept { return address; }

	// SEH-protected dereference for operator->
	// Returns a valid T* or a pointer to a safe zero-initialized static.
	T *operator->() noexcept
	{
		__try
		{
			if (!address)
				return getSafePtr();
			(void)(*reinterpret_cast<volatile const char *>(address));
			return address;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in operator->");
			return getSafePtr();
		}
	}

	T &operator*() noexcept
	{
		__try
		{
			if (!address)
				return *getSafePtr();
			(void)(*reinterpret_cast<volatile const char *>(address));
			return *address;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in operator*");
			return *getSafePtr();
		}
	}

	// ---------- Chaining primitives ----------

	// Follow a pointer-to-pointer: given that this wraps a T* and T contains
	// a member that is a U*, follow it with SEH protection.
	template <typename U, typename V = T, typename = std::enable_if_t<std::is_class_v<V>>>
	Protected_Ptr<U> follow(U *V::*memberPtr) noexcept
	{
		__try
		{
			if (!address)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(address));
			U *target = address->*memberPtr;
			if (!target)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(target));
			return Protected_Ptr<U>(target);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in follow()");
			return Protected_Ptr<U>(nullptr);
		}
	}

	template <typename U>
	Protected_Ptr<U> follow(size_t arrayMemberOffset, size_t index, size_t count) noexcept
	{
		__try
		{
			if (!address)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(address));
			if (index >= count)
				return Protected_Ptr<U>(nullptr);

			// The flexible array member contains U* pointers, so each element is sizeof(U*) bytes.
			auto *base = reinterpret_cast<unsigned char *>(address) + arrayMemberOffset;
			U **ptrArray = reinterpret_cast<U **>(base);
			U *target = ptrArray[index];

			if (!target)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(target));
			return Protected_Ptr<U>(target);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in followFAM()");
			return Protected_Ptr<U>(nullptr);
		}
	}

	// Read a value member (non-pointer) with SEH protection.
	template <typename U, typename V = T, typename = std::enable_if_t<std::is_class_v<V>>>
	U read(U V::*memberPtr) noexcept
	{
		__try
		{
			if (!address)
				return U{};
			(void)(*reinterpret_cast<volatile const char *>(address));
			return address->*memberPtr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in read()");
			return U{};
		}
	}

	// Index into an inline array member that contains pointers (fixed-size).
	template <typename U, size_t N, typename V = T, typename = std::enable_if_t<std::is_class_v<V>>>
	Protected_Ptr<U> at(U *(V::*memberPtr)[N], size_t index) noexcept
	{
		__try
		{
			if (!address)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(address));
			if (index >= N)
				return Protected_Ptr<U>(nullptr);
			U *target = (address->*memberPtr)[index];
			if (!target)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(target));
			return Protected_Ptr<U>(target);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in at()");
			return Protected_Ptr<U>(nullptr);
		}
	}

	// Index into a flexible array member (FAM) that contains values directly.
	// The FAM has no compile-time size, so we use the struct's _Count member
	// for bounds checking.
	template <typename U>
	Protected_Ptr<U> at(size_t arrayMemberOffset, size_t index, size_t count) noexcept
	{
		__try
		{
			if (!address)
				return Protected_Ptr<U>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(address));
			if (index >= count)
				return Protected_Ptr<U>(nullptr);
			// Compute the address of element [index] from the struct base + array member offset.
			auto *base = reinterpret_cast<unsigned char *>(address) + arrayMemberOffset;
			U *target = reinterpret_cast<U *>(base) + index;
			(void)(*reinterpret_cast<volatile const char *>(target));
			return Protected_Ptr<U>(target);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in at() [FAM]");
			return Protected_Ptr<U>(nullptr);
		}
	}

	// General-purpose chain
	template <typename Func>
	auto then(Func &&fn) noexcept -> decltype(fn(std::declval<T *>()))
	{
		using ReturnType = decltype(fn(std::declval<T *>()));
		__try
		{
			if (!address)
				return ReturnType{};
			(void)(*reinterpret_cast<volatile const char *>(address));
			return fn(address);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in then()");
			return ReturnType{};
		}
	}

	// Dereference a pointer-to-pointer
	template <typename U = T, typename = std::enable_if_t<std::is_pointer_v<U>>>
	Protected_Ptr<std::remove_pointer_t<U>> deref() noexcept
	{
		using Inner = std::remove_pointer_t<U>;
		__try
		{
			if (!address)
				return Protected_Ptr<Inner>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(address));
			Inner *target = *address;
			if (!target)
				return Protected_Ptr<Inner>(nullptr);
			(void)(*reinterpret_cast<volatile const char *>(target));
			return Protected_Ptr<Inner>(target);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			protectedLog("Protected_Ptr<T>: SEH exception in deref()");
			return Protected_Ptr<Inner>(nullptr);
		}
	}

private:
	T *address;

	// Safe fallback pointer for types that ARE default-constructible.
	// Returns a pointer to a zero-initialized static instance of T.
	template <typename U = T>
	static std::enable_if_t<std::is_default_constructible_v<U>, U *>
	getSafePtr() noexcept
	{
		static U safeValue{};
		return &safeValue;
	}

	// Safe fallback pointer for types that are NOT default-constructible
	// (e.g., ManagedArray with deleted constructors).
	// Returns a pointer to a zero-initialized block of memory reinterpreted as T*.
	template <typename U = T>
	static std::enable_if_t<!std::is_default_constructible_v<U>, U *>
	getSafePtr() noexcept
	{
		alignas(U) static unsigned char safeBytes[sizeof(U)]{};
		return reinterpret_cast<U *>(safeBytes);
	}
};

// Convenience entry point — wraps a raw pointer.
template <typename T>
Protected_Ptr<T> protect(T *ptr) noexcept
{
	return Protected_Ptr<T>(ptr);
}

#endif
