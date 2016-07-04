// Vk.cpp

#include "stdafx.h"
#include "Vk.h"

static bool GSkipValidation = !false;

static void GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
{
	if (!GSkipValidation)
	{
		uint32 NumLayers;
		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));

		std::vector<VkLayerProperties> InstanceProperties;
		InstanceProperties.resize(NumLayers);

		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, &InstanceProperties[0]));

		for (auto& Prop : InstanceProperties)
		{
			if (!strcmp(Prop.layerName, "VK_LAYER_LUNARG_standard_validation"))
			{
				OutLayers.push_back("VK_LAYER_LUNARG_standard_validation");
				break;
			}
		}
	}

	{
		uint32 NumExtensions;
		checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, nullptr));

		std::vector<VkExtensionProperties> ExtensionsProperties;
		ExtensionsProperties.resize(NumExtensions);

		checkVk(vkEnumerateInstanceExtensionProperties(nullptr, &NumExtensions, &ExtensionsProperties[0]));

		for (auto& Extension : ExtensionsProperties)
		{
			if (!strcmp(Extension.extensionName, "VK_KHR_surface"))
			{
				OutExtensions.push_back("VK_KHR_surface");
			}
			else if (!strcmp(Extension.extensionName, "VK_KHR_win32_surface"))
			{
				OutExtensions.push_back("VK_KHR_win32_surface");
			}
			else if (!strcmp(Extension.extensionName, "VK_EXT_debug_report"))
			{
				OutExtensions.push_back("VK_EXT_debug_report");
			}
		}
	}
}



struct FDevice
{
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties DeviceProperties;
	uint32 PresentQueueFamilyIndex = UINT32_MAX;
	VkQueue PresentQueue = VK_NULL_HANDLE;

	void Create(std::vector<const char*>& Layers)
	{
		VkPhysicalDeviceFeatures DeviceFeatures;
		vkGetPhysicalDeviceFeatures(PhysicalDevice, &DeviceFeatures);

		VkDeviceQueueCreateInfo QueueInfo;
		MemZero(QueueInfo);
		QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		QueueInfo.queueFamilyIndex = PresentQueueFamilyIndex;
		QueueInfo.queueCount = 1;
		float Priorities[1] = { 1.0f };
		QueueInfo.pQueuePriorities = Priorities;

		const char* DeviceExtensions[] = { "VK_KHR_swapchain" };

		VkDeviceCreateInfo DeviceInfo;
		MemZero(DeviceInfo);
		DeviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		DeviceInfo.queueCreateInfoCount = 1;
		DeviceInfo.pQueueCreateInfos = &QueueInfo;
		DeviceInfo.enabledLayerCount = (uint32)Layers.size();
		DeviceInfo.ppEnabledLayerNames = Layers.size() > 0 ? &Layers[0] : nullptr;
		DeviceInfo.enabledExtensionCount = 1;
		DeviceInfo.ppEnabledExtensionNames = DeviceExtensions;
		DeviceInfo.pEnabledFeatures = &DeviceFeatures;
		checkVk(vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device));

		vkGetDeviceQueue(Device, PresentQueueFamilyIndex, 0, &PresentQueue);
	}

	void Destroy()
	{
		vkDestroyDevice(Device, nullptr);
		Device = VK_NULL_HANDLE;
	}
};
FDevice GDevice;

struct FRecyclableResource
{
};

struct FFence : public FRecyclableResource
{
	VkFence Fence = VK_NULL_HANDLE;
	uint64 FenceSignaledCounter = 0;
	VkDevice Device = VK_NULL_HANDLE;

	enum EState
	{
		NotSignaled,
		Signaled,
	};
	EState State = EState::NotSignaled;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;

		VkFenceCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		checkVk(vkCreateFence(Device, &Info, nullptr, &Fence));
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyFence(Device, Fence, nullptr);
		Fence = VK_NULL_HANDLE;
	}

	void Wait()
	{
		check(State == EState::NotSignaled);
		checkVk(vkWaitForFences(Device, 1, &Fence, true, 0xffffffff));
		RefreshState();
	}

	void RefreshState()
	{
		if (State == EState::NotSignaled)
		{
			VkResult Result = vkGetFenceStatus(Device, Fence);
			switch (Result)
			{
			case VK_SUCCESS:
				++FenceSignaledCounter;
				State = EState::Signaled;
				checkVk(vkResetFences(Device, 1, &Fence));
				break;
			case VK_NOT_READY:
				break;
			default:
				checkVk(Result);
				break;
			}
		}
	}
};

struct FSemaphore : public FRecyclableResource
{
	VkSemaphore Semaphore = VK_NULL_HANDLE;

	void Create(VkDevice Device)
	{
		VkSemaphoreCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		checkVk(vkCreateSemaphore(Device, &Info, nullptr, &Semaphore));
	}

	void Destroy(VkDevice Device)
	{
		vkDestroySemaphore(Device, Semaphore, nullptr);
		Semaphore = VK_NULL_HANDLE;
	}
};

struct FCmdBuffer
{
	VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
	FFence Fence;
	VkDevice Device = VK_NULL_HANDLE;

	enum class EState
	{
		ReadyForBegin,
		Beginned,
		Ended,
		Submitted,
	};

	EState State = EState::ReadyForBegin;

	void Create(VkDevice InDevice, VkCommandPool Pool)
	{
		Device = InDevice;

		Fence.Create(Device);

		VkCommandBufferAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		Info.commandPool = Pool;
		Info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		Info.commandBufferCount = 1;

		checkVk(vkAllocateCommandBuffers(Device, &Info, &CmdBuffer));
	}

	void Destroy(VkDevice Device, VkCommandPool Pool)
	{
		Fence.Destroy(Device);
		vkFreeCommandBuffers(Device, Pool, 1, &CmdBuffer);
		CmdBuffer = VK_NULL_HANDLE;
	}

	void Begin()
	{
		check(State == EState::ReadyForBegin);

		VkCommandBufferBeginInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		Info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		checkVk(vkBeginCommandBuffer(CmdBuffer, &Info));

		State = EState::Beginned;
	}

	void End()
	{
		check(State == EState::Beginned);
		checkVk(vkEndCommandBuffer(CmdBuffer));
		State = EState::Ended;
	}

	void WaitForFence()
	{
		check(State == EState::Submitted);
		Fence.Wait();
	}

	void RefreshState()
	{
		if (State == EState::Submitted)
		{
			uint64 PrevCounter = Fence.FenceSignaledCounter;
			Fence.RefreshState();
			if (PrevCounter != Fence.FenceSignaledCounter)
			{
				checkVk(vkResetCommandBuffer(CmdBuffer, 0));
				State = EState::ReadyForBegin;
			}
		}
	}
};

class FCmdBufferFence
{
protected:
	FCmdBuffer* CmdBuffer;
	uint64 FenceSignaledCounter;

public:
	FCmdBufferFence(FCmdBuffer* InCmdBuffer)
	{
		CmdBuffer = InCmdBuffer;
		FenceSignaledCounter = InCmdBuffer->Fence.FenceSignaledCounter;
	}

	bool HasFencePassed() const
	{
		return FenceSignaledCounter < CmdBuffer->Fence.FenceSignaledCounter;
	}
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


struct FCmdBufferMgr
{
	VkCommandPool Pool = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, uint32 QueueFamilyIndex)
	{
		Device = InDevice;

		VkCommandPoolCreateInfo PoolInfo;
		MemZero(PoolInfo);
		PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = QueueFamilyIndex;

		checkVk(vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool));
	}

	void Destroy()
	{
		for (auto* CB : CmdBuffers)
		{
			CB->Destroy(Device, Pool);
			delete CB;
		}
		CmdBuffers.clear();

		vkDestroyCommandPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}

	FCmdBuffer* AllocateCmdBuffer()
	{
		auto* NewCmdBuffer = new FCmdBuffer;
		NewCmdBuffer->Create(Device, Pool);
		CmdBuffers.push_back(NewCmdBuffer);
		return NewCmdBuffer;
	}

	FCmdBuffer* GetActiveCmdBuffer()
	{
		for (auto* CB : CmdBuffers)
		{
			switch (CB->State)
			{
			case FCmdBuffer::EState::Submitted:
				CB->RefreshState();
				break;
			case FCmdBuffer::EState::ReadyForBegin:
				return CB;
			default:
				break;
			}
		}

		return AllocateCmdBuffer();
	}

	void Submit(FCmdBuffer* CmdBuffer, VkQueue Queue, FSemaphore* WaitSemaphore, FSemaphore* SignaledSemaphore)
	{
		check(CmdBuffer->State == FCmdBuffer::EState::Ended);
		VkPipelineStageFlags StageMask[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		Info.pWaitDstStageMask = StageMask;
		Info.commandBufferCount = 1;
		Info.pCommandBuffers = &CmdBuffer->CmdBuffer;
		if (WaitSemaphore)
		{
			Info.waitSemaphoreCount = 1;
			Info.pWaitSemaphores = &WaitSemaphore->Semaphore;
		}
		if (SignaledSemaphore)
		{
			Info.signalSemaphoreCount = 1;
			Info.pSignalSemaphores = &SignaledSemaphore->Semaphore;
		}
		checkVk(vkQueueSubmit(Queue, 1, &Info, CmdBuffer->Fence.Fence));
		CmdBuffer->Fence.State = FFence::EState::NotSignaled;
		CmdBuffer->State = FCmdBuffer::EState::Submitted;
	}

	std::list<FCmdBuffer*> CmdBuffers;
};
FCmdBufferMgr GCmdBufferMgr;

struct FInstance
{
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT DebugReportCB = VK_NULL_HANDLE;

	std::vector<const char*> Layers;

	void CreateInstance()
	{
		VkApplicationInfo AppInfo;
		MemZero(AppInfo);
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pApplicationName = "Test0";
		AppInfo.pEngineName = "Test";
		AppInfo.engineVersion = 1;
		AppInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

		VkInstanceCreateInfo InstanceInfo;
		MemZero(InstanceInfo);
		InstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		InstanceInfo.pApplicationInfo = &AppInfo;

		std::vector<const char*> InstanceLayers;
		std::vector<const char*> InstanceExtensions;
		GetInstanceLayersAndExtensions(InstanceLayers, InstanceExtensions);

		InstanceInfo.enabledLayerCount = (uint32)InstanceLayers.size();
		InstanceInfo.ppEnabledLayerNames = InstanceLayers.size() > 0 ? &InstanceLayers[0] : nullptr;
		InstanceInfo.enabledExtensionCount = (uint32)InstanceExtensions.size();
		InstanceInfo.ppEnabledExtensionNames = InstanceExtensions.size() > 0 ? &InstanceExtensions[0] : nullptr;

		checkVk(vkCreateInstance(&InstanceInfo, nullptr, &Instance));

		Layers = InstanceLayers;
	}

	void DestroyInstance()
	{
		vkDestroyInstance(Instance, nullptr);
		Instance = VK_NULL_HANDLE;
	}

	void CreateSurface(HINSTANCE hInstance, HWND hWnd)
	{
		VkWin32SurfaceCreateInfoKHR SurfaceInfo;
		MemZero(SurfaceInfo);
		SurfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		SurfaceInfo.hinstance = hInstance;
		SurfaceInfo.hwnd = hWnd;
		checkVk(vkCreateWin32SurfaceKHR(Instance, &SurfaceInfo, nullptr, &Surface));
	}

	void DestroySurface()
	{
		vkDestroySurfaceKHR(Instance, Surface, nullptr);
		Surface = VK_NULL_HANDLE;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugReportCallback(
		VkDebugReportFlagsEXT Flags,
		VkDebugReportObjectTypeEXT ObjectType,
		uint64_t Object,
		size_t Location,
		int32_t MessageCode,
		const char* LayerPrefix,
		const char* Message,
		void* UserData)
	{
		if (Flags)
		{
		}

		char s[256];
		sprintf_s(s, "VK: %s\n", Message);
		::OutputDebugStringA(s);
		return true;
	}

	void CreateDebugCallback()
	{
		auto* CreateCB = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(Instance, "vkCreateDebugReportCallbackEXT");
		if (CreateCB)
		{
			VkDebugReportCallbackCreateInfoEXT CreateInfo;
			MemZero(CreateInfo);
			CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			CreateInfo.pfnCallback = &DebugReportCallback;
			CreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT/* | VK_DEBUG_REPORT_DEBUG_BIT_EXT*/;
			checkVk((*CreateCB)(Instance, &CreateInfo, nullptr, &DebugReportCB));
		}
	}

	void DestroyDebugCallback()
	{
		auto* DestroyCB = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(Instance, "vkDestroyDebugReportCallbackEXT");
		if (DestroyCB)
		{
			(*DestroyCB)(Instance, DebugReportCB, nullptr);
		}
	}

	void Create(HINSTANCE hInstance, HWND hWnd)
	{
		CreateInstance();
		CreateDebugCallback();
		CreateSurface(hInstance, hWnd);
	}

	void CreateDevice()
	{
		uint32 NumDevices;
		checkVk(vkEnumeratePhysicalDevices(Instance, &NumDevices, nullptr));
		std::vector<VkPhysicalDevice> Devices;
		Devices.resize(NumDevices);
		checkVk(vkEnumeratePhysicalDevices(Instance, &NumDevices, &Devices[0]));

		for (uint32 Index = 0; Index < NumDevices; ++Index)
		{
			VkPhysicalDeviceProperties DeviceProperties;
			vkGetPhysicalDeviceProperties(Devices[Index], &DeviceProperties);

			uint32 NumQueueFamilies;
			vkGetPhysicalDeviceQueueFamilyProperties(Devices[Index], &NumQueueFamilies, nullptr);
			std::vector<VkQueueFamilyProperties> QueueFamilies;
			QueueFamilies.resize(NumQueueFamilies);
			vkGetPhysicalDeviceQueueFamilyProperties(Devices[Index], &NumQueueFamilies, &QueueFamilies[0]);

			for (uint32 QueueIndex = 0; QueueIndex < NumQueueFamilies; ++QueueIndex)
			{
				VkBool32 bSupportsPresent;
				checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(Devices[Index], QueueIndex, Surface, &bSupportsPresent));
				if (bSupportsPresent && QueueFamilies[QueueIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					GDevice.PhysicalDevice = Devices[Index];
					GDevice.DeviceProperties = DeviceProperties;
					GDevice.PresentQueueFamilyIndex = QueueIndex;
					goto Found;
				}
			}
		}

		// Not found!
		check(0);
		return;

	Found:
		GDevice.Create(Layers);
	}

	void Destroy()
	{
		DestroySurface();
		DestroyDebugCallback();
		DestroyInstance();
	}
};
FInstance GInstance;


struct FImageView
{
	VkImageView ImageView = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, VkImage Image, VkImageViewType ViewType, VkFormat Format, VkImageAspectFlags ImageAspect)
	{
		Device = InDevice;

		VkImageViewCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		Info.image = Image;
		Info.viewType = ViewType;
		Info.format = Format;
		Info.components.r = VK_COMPONENT_SWIZZLE_R;
		Info.components.g = VK_COMPONENT_SWIZZLE_G;
		Info.components.b = VK_COMPONENT_SWIZZLE_B;
		Info.components.a = VK_COMPONENT_SWIZZLE_A;
		Info.subresourceRange.aspectMask = ImageAspect;// VK_IMAGE_ASPECT_COLOR_BIT;
		Info.subresourceRange.levelCount = 1;
		Info.subresourceRange.layerCount = 1;
		checkVk(vkCreateImageView(Device, &Info, nullptr, &ImageView));
	}

	void Destroy()
	{
		vkDestroyImageView(Device, ImageView, nullptr);
		ImageView = VK_NULL_HANDLE;
	}
};

struct FMemAllocation
{
	VkDeviceMemory Mem = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, VkDeviceSize Size, uint32 MemTypeIndex)
	{
		Device = InDevice;

		VkMemoryAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		Info.allocationSize = Size;
		Info.memoryTypeIndex = MemTypeIndex;
	}

	void Destroy()
	{
		vkFreeMemory(Device, Mem, nullptr);
		Mem = VK_NULL_HANDLE;
	}
};

struct FMemManager
{
	FMemAllocation* Alloc(VkDevice InDevice, VkDeviceSize Size, uint32 MemTypeIndex);

	std::map<uint32, std::list<FMemAllocation*>> Allocations;
};

struct FBuffer : public FRecyclableResource
{
	VkBuffer Buffer = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, VkDeviceSize Size)
	{
		Device = InDevice;

		VkBufferCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		Info.size = Size;
		Info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		checkVk(vkCreateBuffer(Device, &Info, nullptr, &Buffer));
	}

	void Destroy()
	{
		vkDestroyBuffer(Device, Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;
	}
};

struct FSwapchain
{
	void Create(VkPhysicalDevice PhysicalDevice, VkDevice InDevice, VkSurfaceKHR Surface, uint32& WindowWidth, uint32& WindowHeight)
	{
		Device = InDevice;

		uint32 NumFormats = 0;
		checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, GInstance.Surface, &NumFormats, nullptr));
		std::vector<VkSurfaceFormatKHR> Formats;
		Formats.resize(NumFormats);
		checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, GInstance.Surface, &NumFormats, &Formats[0]));

		VkFormat ColorFormat;
		check(NumFormats > 0);
		if (NumFormats == 1 && Formats[0].format == VK_FORMAT_UNDEFINED)
		{
			ColorFormat = VK_FORMAT_B8G8R8_UNORM;
		}
		else
		{
			ColorFormat = Formats[0].format;
		}

		VkColorSpaceKHR ColorSpace = Formats[0].colorSpace;

		VkSurfaceCapabilitiesKHR SurfaceCapabilities;
		checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &SurfaceCapabilities));

		uint32 DesiredNumImages = 2;
		if (DesiredNumImages < SurfaceCapabilities.minImageCount)
		{
			DesiredNumImages = SurfaceCapabilities.minImageCount;
		}
		else if (SurfaceCapabilities.maxImageCount != 0 && DesiredNumImages > SurfaceCapabilities.maxImageCount)
		{
			DesiredNumImages = SurfaceCapabilities.maxImageCount;
		}

		SurfaceResolution = SurfaceCapabilities.currentExtent;
		if (SurfaceResolution.width == (uint32)-1)
		{
			SurfaceResolution.width = WindowWidth;
			SurfaceResolution.height = WindowHeight;
		}
		else
		{
			WindowWidth = SurfaceResolution.width;
			WindowHeight = SurfaceResolution.height;
		}

		VkSurfaceTransformFlagBitsKHR Transform = SurfaceCapabilities.currentTransform;
		if (SurfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			Transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		}

		uint32 NumPresentModes;
		checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &NumPresentModes, nullptr));
		std::vector<VkPresentModeKHR> PresentModes;
		PresentModes.resize(NumPresentModes);
		checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &NumPresentModes, &PresentModes[0]));
		VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		for (uint32 Index = 0; Index < NumPresentModes; ++Index)
		{
			if (PresentModes[Index] == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
		}

		VkSwapchainCreateInfoKHR CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		CreateInfo.surface = Surface;
		CreateInfo.minImageCount = DesiredNumImages;
		CreateInfo.imageFormat = ColorFormat;
		CreateInfo.imageColorSpace = ColorSpace;
		CreateInfo.imageExtent = SurfaceResolution;
		CreateInfo.imageArrayLayers = 1;
		CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		CreateInfo.preTransform = Transform;
		CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		CreateInfo.presentMode = PresentMode;
		CreateInfo.clipped = true;
		checkVk(vkCreateSwapchainKHR(Device, &CreateInfo, nullptr, &Swapchain));

		uint32 NumImages;
		checkVk(vkGetSwapchainImagesKHR(Device, Swapchain, &NumImages, nullptr));
		Images.resize(NumImages);
		ImageViews.resize(NumImages);
		AcquiredSemaphores.resize(NumImages);
		RenderingSemaphores.resize(NumImages);
		checkVk(vkGetSwapchainImagesKHR(Device, Swapchain, &NumImages, &Images[0]));

		for (uint32 Index = 0; Index < NumImages; ++Index)
		{
			ImageViews[Index].Create(Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
			AcquiredSemaphores[Index].Create(Device);
			RenderingSemaphores[Index].Create(Device);
		}
	}

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> Images;
	std::vector<FImageView> ImageViews;
	VkDevice Device;
	std::vector<FSemaphore> AcquiredSemaphores;
	std::vector<FSemaphore> RenderingSemaphores;
	uint32 AcquiredSemaphoreIndex = 0;
	uint32 RenderingSemaphoreIndex = 0;
	VkExtent2D SurfaceResolution;

	uint32 AcquiredImageIndex = UINT32_MAX;

	void Destroy()
	{
		for (auto& ImageView : ImageViews)
		{
			ImageView.Destroy();
		}

		vkDestroySwapchainKHR(Device, Swapchain, nullptr);
		Swapchain = VK_NULL_HANDLE;
	}

	void TransitionToPresent(FCmdBuffer* CmdBuffer)
	{
/*
		VkMemoryRequirements Reqs;
		vkGetImageMemoryRequirements(Device, Images[0], &Reqs);

		FBuffer Buffer;
		Buffer.Create(Device, Reqs.size);
		vkCmdFillBuffer(CmdBuffer->CmdBuffer, Buffer.Buffer, 0, Reqs.size, 0);

		VkBufferImageCopy Region;
		MemZero(Region);
		Region.bufferOffset = 0;
		Region.bufferRowLength = SurfaceResolution.width * 4;
		Region.bufferImageHeight = SurfaceResolution.height;
		Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.imageSubresource.layerCount = 1;
		Region.imageExtent.width = SurfaceResolution.width;
		Region.imageExtent.height = SurfaceResolution.height;
*/

		for (uint32 Index = 0; Index < (uint32)Images.size(); ++Index)
		{
/*
			VkImageMemoryBarrier UndefineToTransferBarrier;
			MemZero(UndefineToTransferBarrier);
			UndefineToTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			UndefineToTransferBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			UndefineToTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			UndefineToTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			UndefineToTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			UndefineToTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			UndefineToTransferBarrier.image = Images[Index];
			UndefineToTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			UndefineToTransferBarrier.subresourceRange.layerCount = 1;
			UndefineToTransferBarrier.subresourceRange.levelCount = 1;
			vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &UndefineToTransferBarrier);

			vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, Buffer.Buffer, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
*/

			VkImageMemoryBarrier TransferToPresentBarrier;
			MemZero(TransferToPresentBarrier);
			TransferToPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			TransferToPresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			TransferToPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			TransferToPresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			TransferToPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			TransferToPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			TransferToPresentBarrier.image = Images[Index];
			TransferToPresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			TransferToPresentBarrier.subresourceRange.layerCount = 1;
			TransferToPresentBarrier.subresourceRange.levelCount = 1;
			vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &TransferToPresentBarrier);
		}
	}

	void AcquireNextImage()
	{
		AcquiredSemaphoreIndex = (AcquiredSemaphoreIndex + 1) % AcquiredSemaphores.size();
		VkResult Result = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, AcquiredSemaphores[AcquiredSemaphoreIndex].Semaphore, VK_NULL_HANDLE, &AcquiredImageIndex);
		switch (Result)
		{
		case VK_SUCCESS:
		case VK_SUBOPTIMAL_KHR:
			break;
		case VK_ERROR_OUT_OF_DATE_KHR:
			check(0);
			break;
		default:
			checkVk(Result);
			break;
		}
	}

	void Present(VkQueue PresentQueue, FSemaphore* RenderDone)
	{
		VkPresentInfoKHR Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		Info.waitSemaphoreCount = 1;
		Info.pWaitSemaphores = &AcquiredSemaphores[AcquiredSemaphoreIndex].Semaphore;
		Info.swapchainCount = 1;
		Info.pSwapchains = &Swapchain;
		Info.pImageIndices = &AcquiredImageIndex;
		checkVk(vkQueuePresentKHR(PresentQueue, &Info));
	}
};
FSwapchain GSwapchain;





void DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height)
{
	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice();

	GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	GCmdBufferMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);

	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();
	GSwapchain.TransitionToPresent(CmdBuffer);
	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();

#if 0

	CreateDescriptorLayouts();
	CreatePipelineLayouts();

	CreateRenderPass();

	CreatePipeline();

	CreateFramebuffers();
#endif
}

void DoRender()
{
	auto* CmdBuffer = GCmdBufferMgr.GetActiveCmdBuffer();
	CmdBuffer->Begin();

	FSemaphore RenderDone;
	RenderDone.Create(GDevice.Device);

	GSwapchain.AcquireNextImage();

	VkRenderPass RenderPass;
	{
		VkAttachmentDescription ColorAttachmentDesc;
		MemZero(ColorAttachmentDesc);
		ColorAttachmentDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
		ColorAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		ColorAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		ColorAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		ColorAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		ColorAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		ColorAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		ColorAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference ColorAttachmentRef;
		MemZero(ColorAttachmentRef);
		ColorAttachmentRef.attachment = 0;
		ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription Subpass;
		MemZero(Subpass);
		Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		Subpass.colorAttachmentCount = 1;
		Subpass.pColorAttachments = &ColorAttachmentRef;

		VkRenderPassCreateInfo RenderPassInfo;
		MemZero(RenderPassInfo);
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		RenderPassInfo.attachmentCount = 1;
		RenderPassInfo.pAttachments = &ColorAttachmentDesc;
		RenderPassInfo.subpassCount = 1;
		RenderPassInfo.pSubpasses = &Subpass;

		checkVk(vkCreateRenderPass(GDevice.Device, &RenderPassInfo, nullptr, &RenderPass));
	}

	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, &GSwapchain.AcquiredSemaphores[GSwapchain.AcquiredSemaphoreIndex], &RenderDone);

	GSwapchain.Present(GDevice.PresentQueue, &RenderDone);

	vkDestroyRenderPass(GDevice.Device, RenderPass, nullptr);

	RenderDone.Destroy(GDevice.Device);
}

void DoResize(int32 Width, int32 Height)
{
}

void DoDeinit()
{
	GCmdBufferMgr.Destroy();

	GSwapchain.Destroy();

#if 0
	GResourceRecycler.Deinit();
#endif
	GDevice.Destroy();
	GInstance.Destroy();
}
