
#pragma once

#include "Vk.h"

class FMemSubAlloc;

struct FRange
{
	uint64 Begin;
	uint64 End;
};


class FMemAllocation
{
public:
	FMemAllocation(VkDevice InDevice, VkDeviceSize InSize, uint32 InMemTypeIndex, bool bInMapped)
		: Device(InDevice)
		, Size(InSize)
		, MemTypeIndex(InMemTypeIndex)
		, bMapped(bInMapped)
	{
		VkMemoryAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		Info.allocationSize = Size;
		Info.memoryTypeIndex = MemTypeIndex;
		checkVk(vkAllocateMemory(Device, &Info, nullptr, &Mem));

		if (bMapped)
		{
			checkVk(vkMapMemory(Device, Mem, 0, Size, 0, &MappedMemory));
		}
	}

	void Destroy()
	{
		vkFreeMemory(Device, Mem, nullptr);
		Mem = VK_NULL_HANDLE;
	}

	void* GetMappedMemory()
	{
		check(bMapped);
		return MappedMemory;
	}

	VkDeviceMemory Mem = VK_NULL_HANDLE;

protected:
	VkDevice Device = VK_NULL_HANDLE;
	uint64 Size;
	uint32 MemTypeIndex;
	bool bMapped;
	void* MappedMemory = nullptr;
	friend struct FMemManager;
};

class FMemPage
{
public:
	FMemPage(VkDevice InDevice, VkDeviceSize Size, uint32 InMemTypeIndex, bool bInMapped);

	FMemSubAlloc* TryAlloc(uint64 Size, uint64 Alignment);

	void Release(FMemSubAlloc* SubAlloc);

	VkDeviceMemory GetHandle() const
	{
		return Allocation.Mem;
	}

	void* GetMappedMemory()
	{
		return Allocation.GetMappedMemory();
	}

protected:
	uint32 MemTypeIndex;
	std::vector<FRange> FreeList;
	std::list<FMemSubAlloc*> SubAllocations;

	FMemAllocation Allocation;

	~FMemPage();

	friend struct FMemManager;
};

class FMemSubAlloc
{
public:
	FMemSubAlloc(uint64 InAllocatedOffset, uint64 InAlignedOffset, uint64 InSize, FMemPage* InOwner)
		: AllocatedOffset(InAllocatedOffset)
		, AlignedOffset(InAlignedOffset)
		, Size(InSize)
		, Owner(InOwner)
	{
	}

	uint64 GetBindOffset() const
	{
		return AllocatedOffset;
	}

	VkDeviceMemory GetHandle() const
	{
		return Owner->GetHandle();
	}

	void* GetMappedData()
	{
		auto* AllMapped = (char*)Owner->GetMappedMemory();
		AllMapped += AlignedOffset;
		return AllMapped;
	}

	void Release()
	{
		Owner->Release(this);
	}

protected:
	~FMemSubAlloc() {}

	const uint64 AllocatedOffset;
	const uint64 AlignedOffset;
	const uint64 Size;
	FMemPage* Owner;
	friend class FMemPage;
};

struct FMemManager
{
	void Create(VkDevice InDevice, VkPhysicalDevice PhysicalDevice)
	{
		Device = InDevice;
		vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Properties);
		check(Properties.memoryTypeCount != 0 && Properties.memoryHeapCount != 0);
	}

	void Destroy()
	{
		auto Free = [&](auto& PageMap)
		{
			for (auto& Pair : PageMap)
			{
				for (auto* Page : Pair.second)
				{
					delete Page;
				}
			}
		};
		Free(BufferPages);
		Free(ImagePages);
	}

	uint32 GetMemTypeIndex(uint32 RequestedTypeBits, VkMemoryPropertyFlags PropertyFlags) const
	{
		for (uint32 Index = 0; Index < Properties.memoryTypeCount; ++Index)
		{
			if (RequestedTypeBits & (1 << Index))
			{
				if ((Properties.memoryTypes[Index].propertyFlags & PropertyFlags) == PropertyFlags)
				{
					return Index;
				}
			}
		}

		check(0);
		return (uint32)-1;
	}

	FMemSubAlloc* Alloc(const VkMemoryRequirements& Reqs, VkMemoryPropertyFlags MemPropertyFlags, bool bImage)
	{
		const uint32 MemTypeIndex = GetMemTypeIndex(Reqs.memoryTypeBits, MemPropertyFlags);

		auto& Pages = (bImage ? ImagePages : BufferPages)[MemTypeIndex];
		for (auto& Page : Pages)
		{
			auto* SubAlloc = Page->TryAlloc(Reqs.size, Reqs.alignment);
			if (SubAlloc)
			{
				return SubAlloc;
			}
		}

		const uint64 PageSize = 8 * 1024 * 1024;
		const bool bMapped = (MemPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		auto* NewPage = new FMemPage(Device, PageSize, MemTypeIndex, bMapped);
		Pages.push_back(NewPage);
		auto* SubAlloc = NewPage->TryAlloc(Reqs.size, Reqs.alignment);
		check(SubAlloc);
		return SubAlloc;
	}

	VkPhysicalDeviceMemoryProperties Properties;
	VkDevice Device = VK_NULL_HANDLE;

	std::map<uint32, std::list<FMemPage*>> BufferPages;
	std::map<uint32, std::list<FMemPage*>> ImagePages;
};

struct FRecyclableResource
{
};

#if 0
class FResourceRecycler
{
public:
	enum class EType
	{
		Semaphore,
	};

	void Deinit()
	{
		check(Entries.empty());
	}

	inline void EnqueueResource(FSemaphore* Semaphore, FCmdBuffer* CmdBuffer)
	{
		EnqueueGenericResource(Semaphore, EType::Semaphore, CmdBuffer);
	}

	void Process()
	{
		std::list<FEntry*> NewList;
		for (auto* Entry : Entries)
		{
			if (Entry->HasFencePassed())
			{
				AvailableResources.push_back(Entry);
			}
			else
			{
				NewList.push_back(Entry);
			}
		}

		Entries.swap(NewList);
	}

	FSemaphore* AcquireSemaphore()
	{
		return AcquireGeneric<FSemaphore, EType::Semaphore>();
	}

protected:
	void EnqueueGenericResource(FRecyclableResource* Resource, EType Type, FCmdBuffer* CmdBuffer)
	{
		auto* NewEntry = new FEntry(CmdBuffer, Type, Resource);
		Entries.push_back(NewEntry);
	}

	template <typename T, EType Type>
	T* AcquireGeneric()
	{
		for (auto* Entry : AvailableResources)
		{
			if (Entry->Type == Type)
			{
				T* Found = Entry->Resource;
				AvailableResources.remove(Entry);
				delete Entry;
				return Found;
			}
		}

		return nullptr;
	}

	struct FEntry : public FCmdBufferFence
	{
		FRecyclableResource* Resource;
		EType Type;

		FEntry(FCmdBuffer* InBuffer, EType InType, FRecyclableResource* InResource)
			: FCmdBufferFence(InBuffer)
			, Resource(InResource)
			, Type(InType)
		{
		}
	};

	std::list<FEntry*> Entries;
	std::list<FEntry*> AvailableResources;
};

FResourceRecycler GResourceRecycler;
#endif
