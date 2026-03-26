#ifndef SRTPLUGINRE9_GAMEOBJECTS_H
#define SRTPLUGINRE9_GAMEOBJECTS_H

#include <cstdint>

#pragma warning(push)
#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
template <typename T>
struct ManagedArray // 0x20+ (32+)
{
	// Prevent direct instantiation - this struct is only valid when overlaid onto existing memory via casting.
	ManagedArray() = delete;
	ManagedArray(const ManagedArray &) = delete;
	ManagedArray(ManagedArray &&) = delete;
	ManagedArray &operator=(const ManagedArray &) = delete;
	ManagedArray &operator=(ManagedArray &&) = delete;

	// Prevent heap allocation/deallocation.
	void *operator new(size_t) = delete;
	void *operator new[](size_t) = delete;
	void operator delete(void *) = delete;
	void operator delete[](void *) = delete;

	const uint8_t _unknown1[0x10]; // 0x00 - 0x09 (10) Unknown bytes
	const uintptr_t FieldPtr;      // 0x10 - 0x17 (8) Field Pointer?
	const uint8_t _unknown2[0x4];  // 0x18 - 0x1B (4) Unknown bytes
	const uint32_t _Count;         // 0x1C - 0x1F (4)  Array size
	T _Values[];                   // 0x20 - 0x?? (??) Array elements

	const uint32_t Count() const
	{
		return _Count;
	}

	// Array accessors.
	T &operator[](const uint32_t index)
	{
		return _Values[index];
	}

	const T &operator[](const uint32_t index) const
	{
		return _Values[index];
	}

	// Iterator support.
	T *begin() { return &_Values[0]; }
	T *end() { return &_Values[_Count]; }
	const T *begin() const { return &_Values[0]; }
	const T *end() const { return &_Values[_Count]; }
};
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
template <typename T>
struct ManagedList // 0x20 (32)
{
	// Prevent direct instantiation - this struct is only valid when overlaid onto existing memory via casting.
	ManagedList() = delete;
	ManagedList(const ManagedList &) = delete;
	ManagedList(ManagedList &&) = delete;
	ManagedList &operator=(const ManagedList &) = delete;
	ManagedList &operator=(ManagedList &&) = delete;

	// Prevent heap allocation/deallocation.
	void *operator new(size_t) = delete;
	void *operator new[](size_t) = delete;
	void operator delete(void *) = delete;
	void operator delete[](void *) = delete;

	const uint8_t _unknown1[0x10]; // 0x00 - 0x09 (10) Unknown bytes
	ManagedArray<T> *Values;       // 0x10 - 0x17 (8) Values
	const int32_t Size;            // 0x18 - 0x1B (4) Size
	const int32_t Version;         // 0x1C - 0x1F (4) Version?

	const uint32_t Count() const
	{
		return Size;
	}

	// Array accessors.
	T &operator[](const uint32_t index)
	{
		return Values->operator[][index];
	}

	const T &operator[](const uint32_t index) const
	{
		return Values->operator[][index];
	}

	// Iterator support.
	T *begin() { return &Values[0][0]; }
	T *end() { return &Values[0][Size]; }
	const T *begin() const { return &Values[0][0]; }
	const T *end() const { return &Values[0][Size]; }
};
#pragma warning(pop)

struct Vec3 // 0x10 (16)
{
	float_t x;              // 0x00 - 0x03 (4)
	float_t y;              // 0x04 - 0x07 (4)
	float_t z;              // 0x08 - 0x0B (4)
	uint8_t _unknown1[0x4]; // 0x0C - 0x0F (4)
};

struct Time // 0x20 (32)
{
	uint8_t _unknown1[0x10]; // 0x00 - 0x0F (16)
	uint64_t _ElapsedTime;   // 0x10 - 0x17 (8)
	uint8_t _unknown2[0x8];  // 0x18 - 0x1F (8)
};

struct GameClock // 0x24 (36)
{
	uint8_t _unknown1[0x10];       // 0x00 - 0x0F (16)
	ManagedArray<Time *> *_Timers; // 0x10 - 0x17 (8)
	uint8_t _unknown2[0x8];        // 0x18 - 0x1F (8)
	uint32_t _RunningTimers;       // 0x20 - 0x23 (4)
};

struct RankProfile // 0x38 (-)
{
	uint8_t _unknown1[0x14]; // 0x00 - 0x13 (-)
	int32_t _RankPoints;     // 0x14 - 0x17 (-)
	int32_t _CurrentRank;    // 0x18 - 0x21 (-)
};

struct RankManager // 0x38 (-)
{
	uint8_t _unknown1[0x30];         // 0x00 - 0x2F (-)
	RankProfile *_ActiveRankProfile; // 0x30 - 0x37 (8)
};

// *** Char Mgr

struct CharacterHitPointData // 0x38 (56)
{
	uint8_t _unknown1[0x20];                  // 0x00 - 0x1F (32)
	ManagedArray<int32_t *> *_MaximumHPTable; // 0x20 - 0x27 (8)
	int32_t _CurrentHP;                       // 0x28 - 0x2B (4)
	int32_t _CurrentHPLevel;                  // 0x2C - 0x2F (4)
	int32_t _CurrentMaxHP;                    // 0x30 - 0x33 (4)
	int32_t _IsSetuped;                       // 0x34 - 0x37 (4) (System.Boolean)
};

struct HitPoint // 0x80 (128)
{
	uint8_t _unknown1[0x10];             // 0x00 - 0x0F (16)
	CharacterHitPointData *HitPointData; // 0x10 - 0x17 (8)
	uint8_t _unknown2[0x68];             // 0x18 - 0x7F (104)
};

struct CharacterContext // 0xF8 (248)
{
	uint8_t _unknown1[0x40]; // 0x00 - 0x3F (64)
	uint16_t KindID;         // 0x40 - 0x41 (2) (entries from app.CharacterKindID static fields?)
	uint8_t _unknown2[0x2E]; // 0x42 - 0x6F (46)
	HitPoint *HitPoint;      // 0x70 - 0x77 (8)
	uint8_t _unknown3[0x80]; // 0x78 - 0xF7 (128)
};

struct PlayerContext : CharacterContext // 0x150 (336)
{
	uint8_t _unknown4[0x8];  // 0xF8 - 0xFF (8)
	Vec3 PositionFast;       // 0x100 - 0x10F (16)
	uint8_t _unknown5[0x40]; // 0x110 - 0x14F (64)
};

struct EnemyContext : CharacterContext // 0x138 (312)
{
	uint8_t _unknown4[0x40]; // 0xF8 - 0x137 (64)
};

struct CharacterManager // 0x1F0 (496)
{
	uint8_t _unknown1[0xB0];                       // 0x00 - 0xAF (176)
	PlayerContext *PlayerContextFast;              // 0xB0 - 0xB7 (8)
	ManagedList<EnemyContext *> *EnemyContextList; // 0xB8 - 0xBF (8)
	uint8_t _unknown2[0x130];                      // 0xC0 - 0x1EF (304)
};

#endif
