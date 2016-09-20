// Base Vulkan header classes

#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "../Utils/Util.h"

struct FInstance
{
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkInstance Instance = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT DebugReportCB = VK_NULL_HANDLE;

	std::vector<const char*> Layers;

	void GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions);

	void CreateInstance()
	{
		VkApplicationInfo AppInfo;
		MemZero(AppInfo);
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pApplicationName = "Test0";
		AppInfo.pEngineName = "VkTest";
		AppInfo.engineVersion = 1;
		AppInfo.apiVersion = VK_API_VERSION_1_0;

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
		void* UserData);

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

	void CreateDevice(struct FDevice& OutDevice);

	void Destroy()
	{
		DestroySurface();
		DestroyDebugCallback();
		DestroyInstance();
	}
};

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

struct FFence
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

	void Wait(uint64 TimeInNanoseconds = 0xffffffff)
	{
		check(State == EState::NotSignaled);
		checkVk(vkWaitForFences(Device, 1, &Fence, true, TimeInNanoseconds));
		RefreshState();
	}

	bool IsNotSignaled() const
	{
		return State == EState::NotSignaled;
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

struct FCmdBuffer
{
	VkCommandBuffer CmdBuffer = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	FFence* Fence = nullptr;

	enum class EState
	{
		ReadyForBegin,
		Begun,
		Ended,
		Submitted,
		InsideRenderPass,
	};
	EState State = EState::ReadyForBegin;

	virtual void Destroy(VkDevice Device, VkCommandPool Pool)
	{
		if (State == EState::Submitted)
		{
			RefreshState();
			if (Fence->IsNotSignaled())
			{
				const uint64 TimeToWaitInNanoseconds = 5;
				Fence->Wait(TimeToWaitInNanoseconds);
			}
			RefreshState();
		}
		vkFreeCommandBuffers(Device, Pool, 1, &CmdBuffer);
		CmdBuffer = VK_NULL_HANDLE;
	}

	void BeginRenderPass(VkRenderPass RenderPass, const struct FFramebuffer& Framebuffer, bool bHasSecondary);

	void EndRenderPass()
	{
		check(State == EState::InsideRenderPass);

		vkCmdEndRenderPass(CmdBuffer);

		State = EState::Begun;
	}

	void WaitForFence()
	{
		if (State == EState::Submitted)
		{
			Fence->Wait();
			RefreshState();
		}
	}

	void RefreshState()
	{
		if (State == EState::Submitted)
		{
			uint64 PrevCounter = Fence->FenceSignaledCounter;
			Fence->RefreshState();
			if (PrevCounter != Fence->FenceSignaledCounter)
			{
				checkVk(vkResetCommandBuffer(CmdBuffer, 0));
				State = EState::ReadyForBegin;
			}
		}
	}

	virtual struct FSecondaryCmdBuffer* GetSecondary()
	{
		return nullptr;
	}

	void End()
	{
		check(State == EState::Begun);
		checkVk(vkEndCommandBuffer(CmdBuffer));
		State = EState::Ended;
	}
};

struct FPrimaryCmdBuffer : public FCmdBuffer
{
	FFence PrimaryFence;

	void Create(VkDevice InDevice, VkCommandPool Pool)
	{
		Device = InDevice;

		PrimaryFence.Create(Device);
		Fence = &PrimaryFence;

		VkCommandBufferAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		Info.commandPool = Pool;
		Info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		Info.commandBufferCount = 1;

		checkVk(vkAllocateCommandBuffers(Device, &Info, &CmdBuffer));
	}

	void Begin()
	{
		check(State == EState::ReadyForBegin);

		VkCommandBufferBeginInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		Info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		checkVk(vkBeginCommandBuffer(CmdBuffer, &Info));

		State = EState::Begun;
	}

	void ExecuteSecondary()
	{
		check(State != FCmdBuffer::EState::Ended && State != FCmdBuffer::EState::Submitted);
		if (!Secondary.empty())
		{
			vkCmdExecuteCommands(CmdBuffer, (uint32)Secondary.size(), &SecondaryList[0]);
			Secondary.resize(0);
			SecondaryList.resize(0);
		}
	}

	virtual void Destroy(VkDevice Device, VkCommandPool Pool) override
	{
		FCmdBuffer::Destroy(Device, Pool);
		Fence->Destroy(Device);
	}

	std::list<struct FSecondaryCmdBuffer*> Secondary;
	std::vector<VkCommandBuffer> SecondaryList;
};

struct FSecondaryCmdBuffer : public FCmdBuffer
{
	void BeginSecondary(FPrimaryCmdBuffer* ParentCmdBuffer, VkRenderPass RenderPass)
	{
		check(State == EState::ReadyForBegin);

		VkCommandBufferInheritanceInfo Inheritance;
		MemZero(Inheritance);
		Inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		//uint32_t                         subpass;
		//VkFramebuffer                    framebuffer;
		//VkBool32                         occlusionQueryEnable;
		//VkQueryControlFlags              queryFlags;
		//VkQueryPipelineStatisticFlags    pipelineStatistics;

		VkCommandBufferBeginInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		Info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (RenderPass != VK_NULL_HANDLE)
		{
			Info.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			Inheritance.renderPass = RenderPass;
		}
		Info.pInheritanceInfo = &Inheritance;
		checkVk(vkBeginCommandBuffer(CmdBuffer, &Info));

		State = EState::Begun;

		ParentCmdBuffer->Secondary.push_back(this);
		ParentCmdBuffer->SecondaryList.push_back(CmdBuffer);
	}

	void CreateSecondary(VkDevice InDevice, VkCommandPool Pool, FFence* ParentFence)
	{
		Device = InDevice;

		VkCommandBufferAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		Info.commandPool = Pool;
		Info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		Info.commandBufferCount = 1;

		checkVk(vkAllocateCommandBuffers(Device, &Info, &CmdBuffer));
	}

	virtual struct FSecondaryCmdBuffer* GetSecondary()
	{
		return this;
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
		FenceSignaledCounter = InCmdBuffer->Fence->FenceSignaledCounter;
	}

	bool HasFencePassed() const
	{
		return FenceSignaledCounter < CmdBuffer->Fence->FenceSignaledCounter;
	}
};

struct FSemaphore
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
			CB->RefreshState();
			CB->Destroy(Device, Pool);
			delete CB;
		}
		CmdBuffers.clear();

		for (auto* CB : SecondaryCmdBuffers)
		{
			CB->RefreshState();
			CB->Destroy(Device, Pool);
			delete CB;
		}
		SecondaryCmdBuffers.clear();

		vkDestroyCommandPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}

	FPrimaryCmdBuffer* AllocateCmdBuffer()
	{
		for (auto* CmdBuffer : CmdBuffers)
		{
			CmdBuffer->RefreshState();
			if (CmdBuffer->State == FPrimaryCmdBuffer::EState::ReadyForBegin)
			{
				return CmdBuffer;
			}
		}

		auto* NewCmdBuffer = new FPrimaryCmdBuffer;
		NewCmdBuffer->Create(Device, Pool);
		CmdBuffers.push_back(NewCmdBuffer);
		return NewCmdBuffer;
	}

	FSecondaryCmdBuffer* AllocateSecondaryCmdBuffer(FFence* ParentFence)
	{
		for (auto* CmdBuffer : SecondaryCmdBuffers)
		{
			CmdBuffer->RefreshState();
			if (CmdBuffer->State == FPrimaryCmdBuffer::EState::ReadyForBegin)
			{
				CmdBuffer->Fence = ParentFence;
				return CmdBuffer;
			}
		}

		auto* NewCmdBuffer = new FSecondaryCmdBuffer;
		NewCmdBuffer->CreateSecondary(Device, Pool, ParentFence);
		SecondaryCmdBuffers.push_back(NewCmdBuffer);
		return NewCmdBuffer;
	}
/*
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

		for (auto* CB : SecondaryCmdBuffers)
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
	}*/

	FPrimaryCmdBuffer* GetActivePrimaryCmdBuffer()
	{
		for (auto* CB : CmdBuffers)
		{
			switch (CB->State)
			{
			case FPrimaryCmdBuffer::EState::Submitted:
				CB->RefreshState();
				break;
			case FPrimaryCmdBuffer::EState::ReadyForBegin:
				return CB;
			default:
				break;
			}
		}

		return AllocateCmdBuffer();
	}

	void Submit(FPrimaryCmdBuffer* CmdBuffer, VkQueue Queue, FSemaphore* WaitSemaphore, FSemaphore* SignaledSemaphore)
	{
		check(CmdBuffer->State == FPrimaryCmdBuffer::EState::Ended);
		check(CmdBuffer->Secondary.empty());
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
		checkVk(vkQueueSubmit(Queue, 1, &Info, CmdBuffer->Fence->Fence));
		CmdBuffer->Fence->State = FFence::EState::NotSignaled;
		CmdBuffer->State = FPrimaryCmdBuffer::EState::Submitted;
		Update();
	}

	void Update()
	{
		for (auto* CmdBuffer : CmdBuffers)
		{
			CmdBuffer->RefreshState();
		}

		for (auto* CmdBuffer : SecondaryCmdBuffers)
		{
			CmdBuffer->RefreshState();
		}
	}

	std::list<FPrimaryCmdBuffer*> CmdBuffers;
	std::list<FSecondaryCmdBuffer*> SecondaryCmdBuffers;
};

static inline uint32 GetFormatBitsPerPixel(VkFormat Format)
{
	switch (Format)
	{
	case VK_FORMAT_R32_SFLOAT:
		return 32;

	default:
		break;
	}
	check(0);
	return 0;
}
