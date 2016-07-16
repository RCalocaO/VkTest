// Vk.cpp

#include "stdafx.h"
#include "Vk.h"

struct FCmdBuffer;

static bool GSkipValidation = false;

void TransitionImage(FCmdBuffer* CmdBuffer, VkImage Image, VkImageLayout SrcLayout, VkAccessFlags SrcMask, VkImageLayout DestLayout, VkAccessFlags DstMask, VkImageAspectFlags AspectMask);

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


struct FShader
{
	bool Create(const char* Filename, VkDevice Device)
	{
		SpirV = LoadFile(Filename);
		if (SpirV.empty())
		{
			return false;
		}

		VkShaderModuleCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		check(SpirV.size() % 4 == 0);
		CreateInfo.codeSize = SpirV.size();
		CreateInfo.pCode = (uint32*)&SpirV[0];

		checkVk(vkCreateShaderModule(Device, &CreateInfo, nullptr, &ShaderModule));

		return true;
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyShaderModule(Device, ShaderModule, nullptr);
		ShaderModule = VK_NULL_HANDLE;
	}

	std::vector<char> SpirV;
	VkShaderModule ShaderModule;
};
static FShader GVertexShader;
static FShader GPixelShader;


struct FVertex
{
	float x, y, z;
	uint32 Color;
};

struct FVertexBuffer
{
	void Create(VkDevice InDevice)
	{
		Device = InDevice;
		VkBufferCreateInfo BufferInfo;
		MemZero(BufferInfo);
		BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		BufferInfo.size = sizeof(FVertex) * 3;
		BufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		//BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		checkVk(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buffer));
	}

	inline VkMemoryRequirements GetMemReqs()
	{
		VkMemoryRequirements Reqs;
		vkGetBufferMemoryRequirements(Device, Buffer, &Reqs);
		return Reqs;
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyBuffer(Device, Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;
	}

	VkDevice Device;
	VkBuffer Buffer = VK_NULL_HANDLE;
};
FVertexBuffer GVB;

struct FGfxPipeline
{
	void Create(VkDevice Device, FShader* VS, FShader* PS, uint32 Width, uint32 Height, VkRenderPass RenderPass)
	{
		VkPipelineLayoutCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		checkVk(vkCreatePipelineLayout(Device, &CreateInfo, nullptr, &PipelineLayout));

		VkPipelineShaderStageCreateInfo ShaderInfo[2];
		MemZero(ShaderInfo);
		{
			VkPipelineShaderStageCreateInfo* Info = ShaderInfo;
			check(VS);
			Info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			Info->stage = VK_SHADER_STAGE_VERTEX_BIT;
			Info->module = VS->ShaderModule;
			Info->pName = "main";
			++Info;

			if (PS)
			{
				Info->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				Info->stage = VK_SHADER_STAGE_FRAGMENT_BIT;
				Info->module = PS->ShaderModule;
				Info->pName = "main";
				++Info;
			}
		}

		VkVertexInputBindingDescription VBDesc;
		MemZero(VBDesc);
		VBDesc.binding = 0;
		VBDesc.stride = sizeof(FVertex);
		VBDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription VIADesc[2];
		MemZero(VIADesc);
		//VIADesc.location = 0;
		VIADesc[0].binding = 0;
		VIADesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		VIADesc[0].offset = 0;
		VIADesc[1].binding = 0;
		VIADesc[1].location = 1;
		VIADesc[1].format = VK_FORMAT_R8G8B8A8_UNORM;
		VIADesc[1].offset = 12;

		VkPipelineVertexInputStateCreateInfo VIInfo;
		MemZero(VIInfo);
		VIInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VIInfo.vertexBindingDescriptionCount = 1;
		VIInfo.pVertexBindingDescriptions = &VBDesc;
		VIInfo.vertexAttributeDescriptionCount = 2;
		VIInfo.pVertexAttributeDescriptions = VIADesc;

		VkPipelineInputAssemblyStateCreateInfo IAInfo;
		MemZero(IAInfo);
		IAInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		IAInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		//IAInfo.primitiveRestartEnable = VK_FALSE;

		VkViewport Viewport;
		MemZero(Viewport);
		//Viewport.x = 0;
		//Viewport.y = 0;
		Viewport.width = (float)Width;
		Viewport.height = (float)Height;
		//Viewport.minDepth = 0;
		Viewport.maxDepth = 1;

		VkRect2D Scissor;
		MemZero(Scissor);
		//scissors.offset = { 0, 0 };
		Scissor.extent = { Width, Height };

		VkPipelineViewportStateCreateInfo ViewportInfo;
		MemZero(ViewportInfo);
		ViewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		ViewportInfo.viewportCount = 1;
		ViewportInfo.pViewports = &Viewport;
		ViewportInfo.scissorCount = 1;
		ViewportInfo.pScissors = &Scissor;

		VkPipelineRasterizationStateCreateInfo RSInfo;
		MemZero(RSInfo);
		RSInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		//RSInfo.depthClampEnable = VK_FALSE;
		//RSInfo.rasterizerDiscardEnable = VK_FALSE;
		RSInfo.polygonMode = VK_POLYGON_MODE_FILL;
		RSInfo.cullMode = VK_CULL_MODE_NONE;
		RSInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		//RSInfo.depthBiasEnable = VK_FALSE;
		//RSInfo.depthBiasConstantFactor = 0;
		//RSInfo.depthBiasClamp = 0;
		//RSInfo.depthBiasSlopeFactor = 0;
		RSInfo.lineWidth = 1;

		VkPipelineMultisampleStateCreateInfo MSInfo;
		MemZero(MSInfo);
		MSInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		MSInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		//MSInfo.sampleShadingEnable = VK_FALSE;
		//MSInfo.minSampleShading = 0;
		//MSInfo.pSampleMask = NULL;
		//MSInfo.alphaToCoverageEnable = VK_FALSE;
		//MSInfo.alphaToOneEnable = VK_FALSE;

		VkStencilOpState Stencil;
		MemZero(Stencil);
		Stencil.failOp = VK_STENCIL_OP_KEEP;
		Stencil.passOp = VK_STENCIL_OP_KEEP;
		Stencil.depthFailOp = VK_STENCIL_OP_KEEP;
		Stencil.compareOp = VK_COMPARE_OP_ALWAYS;
		//Stencil.compareMask = 0;
		//Stencil.writeMask = 0;
		//Stencil.reference = 0;

		VkPipelineDepthStencilStateCreateInfo DSInfo;
		MemZero(DSInfo);
		DSInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		DSInfo.depthTestEnable = VK_TRUE;
		DSInfo.depthWriteEnable = VK_TRUE;
		DSInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		//DSInfo.depthBoundsTestEnable = VK_FALSE;
		//DSInfo.stencilTestEnable = VK_FALSE;
		DSInfo.front = Stencil;
		DSInfo.back = Stencil;
		//DSInfo.minDepthBounds = 0;
		//DSInfo.maxDepthBounds = 0;

		VkPipelineColorBlendAttachmentState AttachState;
		MemZero(AttachState);
		//AtachState.blendEnable = VK_FALSE;
		AttachState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		AttachState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		AttachState.colorBlendOp = VK_BLEND_OP_ADD;
		AttachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		AttachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		AttachState.alphaBlendOp = VK_BLEND_OP_ADD;
		AttachState.colorWriteMask = 0xf;

		VkPipelineColorBlendStateCreateInfo CBInfo;
		MemZero(CBInfo);
		CBInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		//CBInfo.logicOpEnable = VK_FALSE;
		CBInfo.logicOp = VK_LOGIC_OP_CLEAR;
		CBInfo.attachmentCount = 1;
		CBInfo.pAttachments = &AttachState;
		//CBInfo.blendConstants[0] = 0.0;
		//CBInfo.blendConstants[1] = 0.0;
		//CBInfo.blendConstants[2] = 0.0;
		//CBInfo.blendConstants[3] = 0.0;

		VkDynamicState Dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo DynamicInfo;
		MemZero(DynamicInfo);
		DynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		DynamicInfo.dynamicStateCount = 2;
		DynamicInfo.pDynamicStates = Dynamic;

		VkGraphicsPipelineCreateInfo PipelineInfo;
		MemZero(PipelineInfo);
		PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		PipelineInfo.stageCount = 2;
		PipelineInfo.pStages = ShaderInfo;
		PipelineInfo.pVertexInputState = &VIInfo;
		PipelineInfo.pInputAssemblyState = &IAInfo;
		PipelineInfo.pTessellationState = NULL;
		PipelineInfo.pViewportState = &ViewportInfo;
		PipelineInfo.pRasterizationState = &RSInfo;
		PipelineInfo.pMultisampleState = &MSInfo;
		PipelineInfo.pDepthStencilState = &DSInfo;
		PipelineInfo.pColorBlendState = &CBInfo;
		PipelineInfo.pDynamicState = &DynamicInfo;
		PipelineInfo.layout = PipelineLayout;
		PipelineInfo.renderPass = RenderPass;
		//PipelineInfo.subpass = 0;
		//PipelineInfo.basePipelineHandle = NULL;
		//PipelineInfo.basePipelineIndex = 0;

		checkVk(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline));
	}

	VkPipeline Pipeline = VK_NULL_HANDLE;

	void Destroy(VkDevice Device)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;

		vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
		PipelineLayout = VK_NULL_HANDLE;
	}

	VkPipelineLayout PipelineLayout;
};

static FGfxPipeline GGfxPipeline;

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
		InsideRenderPass,
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
		if (State == EState::Submitted)
		{
			RefreshState();
			if (Fence.IsNotSignaled())
			{
				const uint64 TimeToWaitInNanoseconds = 5;
				Fence.Wait(TimeToWaitInNanoseconds);
			}
			RefreshState();
		}
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

	void BeginRenderPass(VkRenderPass RenderPass, const struct FFramebuffer& Framebuffer);

	void EndRenderPass()
	{
		check(State == EState::InsideRenderPass);

		vkCmdEndRenderPass(CmdBuffer);

		State = EState::Beginned;
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
			CB->RefreshState();
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
		char s[2048];
		int n = 0;
		if (Flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
		{
			sprintf_s(s, "<VK>Error: %s\n", Message);
			++n;
			::OutputDebugStringA(s);
		}
		else if (Flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
		{
			sprintf_s(s, "<VK>Warn: %s\n", Message);
			++n;
			::OutputDebugStringA(s);
		}
		else
		{
			//sprintf_s(s, "<VK>: %s\n", Message);
			//::OutputDebugStringA(s);
		}

		return false;
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
		checkVk(vkAllocateMemory(Device, &Info, nullptr, &Mem));
	}

	void Destroy()
	{
		vkFreeMemory(Device, Mem, nullptr);
		Mem = VK_NULL_HANDLE;
	}
};

struct FMemManager
{
	//FMemAllocation* Alloc(VkDevice InDevice, VkDeviceSize Size, uint32 MemTypeIndex);

	void Create(VkDevice InDevice, VkPhysicalDevice PhysicalDevice)
	{
		Device = InDevice;
		vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Properties);
	}

	void Destroy()
	{
		for (auto& Pair : Allocations)
		{
			for (auto* Alloc : Pair.second)
			{
				Alloc->Destroy();
				delete Alloc;
			}
		}
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

	FMemAllocation* Alloc(const VkMemoryRequirements& Reqs)
	{
		const uint32 MemTypeIndex = GetMemTypeIndex(Reqs.memoryTypeBits, /*VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | */VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		auto* MemAlloc = new FMemAllocation;
		MemAlloc->Create(Device, Reqs.size, MemTypeIndex);
		Allocations[MemTypeIndex].push_back(MemAlloc);
		return MemAlloc;
	}

	VkPhysicalDeviceMemoryProperties Properties;
	VkDevice Device = VK_NULL_HANDLE;
	std::map<uint32, std::list<FMemAllocation*>> Allocations;
};
FMemManager GMemMgr;

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
		PresentCompleteSemaphores.resize(NumImages);
		RenderingSemaphores.resize(NumImages);
		checkVk(vkGetSwapchainImagesKHR(Device, Swapchain, &NumImages, &Images[0]));

		for (uint32 Index = 0; Index < NumImages; ++Index)
		{
			ImageViews[Index].Create(Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
			PresentCompleteSemaphores[Index].Create(Device);
			RenderingSemaphores[Index].Create(Device);
		}
	}

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> Images;
	std::vector<FImageView> ImageViews;
	VkDevice Device = VK_NULL_HANDLE;
	std::vector<FSemaphore> PresentCompleteSemaphores;
	std::vector<FSemaphore> RenderingSemaphores;
	uint32 PresentCompleteSemaphoreIndex = 0;
	uint32 RenderingSemaphoreIndex = 0;
	VkExtent2D SurfaceResolution;

	uint32 AcquiredImageIndex = UINT32_MAX;

	void Destroy()
	{
		for (auto& RS : RenderingSemaphores)
		{
			RS.Destroy(Device);
		}

		for (auto& PS : PresentCompleteSemaphores)
		{
			PS.Destroy(Device);
		}

		for (auto& ImageView : ImageViews)
		{
			ImageView.Destroy();
		}

		vkDestroySwapchainKHR(Device, Swapchain, nullptr);
		Swapchain = VK_NULL_HANDLE;
	}

	void ClearAndTransitionToPresent(FCmdBuffer* CmdBuffer)
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
		VkClearColorValue Color;
		MemZero(Color);
		VkImageSubresourceRange Range;
		MemZero(Range);
		Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Range.levelCount = 1;
		Range.layerCount = 1;
		for (uint32 Index = 0; Index < (uint32)Images.size(); ++Index)
		{
			TransitionImage(CmdBuffer, Images[Index], VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
			vkCmdClearColorImage(CmdBuffer->CmdBuffer, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
			TransitionImage(CmdBuffer, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
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

/*
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
*/

		}
	}

	void AcquireNextImage()
	{
		PresentCompleteSemaphoreIndex = (PresentCompleteSemaphoreIndex + 1) % PresentCompleteSemaphores.size();
		VkResult Result = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, PresentCompleteSemaphores[PresentCompleteSemaphoreIndex].Semaphore, VK_NULL_HANDLE, &AcquiredImageIndex);
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

	void Present(VkQueue PresentQueue)
	{
		VkPresentInfoKHR Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		Info.waitSemaphoreCount = 1;
		Info.pWaitSemaphores = &RenderingSemaphores[AcquiredImageIndex].Semaphore;
		Info.swapchainCount = 1;
		Info.pSwapchains = &Swapchain;
		Info.pImageIndices = &AcquiredImageIndex;
		checkVk(vkQueuePresentKHR(PresentQueue, &Info));
	}
};
FSwapchain GSwapchain;


struct FFramebuffer : public FRecyclableResource
{
	VkFramebuffer Framebuffer = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void CreateColorOnly(VkDevice InDevice, VkRenderPass RenderPass, VkImageView ColorAttachment, uint32 InWidth, uint32 InHeight)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		VkFramebufferCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		CreateInfo.renderPass = RenderPass;
		CreateInfo.attachmentCount = 1;
		CreateInfo.pAttachments = &ColorAttachment;
		CreateInfo.width = Width;
		CreateInfo.height = Height;
		CreateInfo.layers = 1;

		checkVk(vkCreateFramebuffer(Device, &CreateInfo, nullptr, &Framebuffer));
	}

	void Destroy()
	{
		vkDestroyFramebuffer(Device, Framebuffer, nullptr);
		Framebuffer = VK_NULL_HANDLE;
	}

	uint32 Width = 0;
	uint32 Height = 0;
};


void FCmdBuffer::BeginRenderPass(VkRenderPass RenderPass, const FFramebuffer& Framebuffer)
{
	check(State == EState::Beginned);

	static uint32 N = 0;
	N = (N  + 1) % 256;

	VkClearValue ClearValues[] = { { N / 255.0f, 1.0f, 0.0f, 1.0f },{ 1.0, 0.0 } };
	VkRenderPassBeginInfo BeginInfo = {};
	BeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	BeginInfo.renderPass = RenderPass;
	BeginInfo.framebuffer = Framebuffer.Framebuffer;
	BeginInfo.renderArea = { 0, 0, Framebuffer.Width, Framebuffer.Height };
	BeginInfo.clearValueCount = 1;
	BeginInfo.pClearValues = ClearValues;
	vkCmdBeginRenderPass(CmdBuffer, &BeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	State = EState::InsideRenderPass;
}

struct FRenderPass : public FRecyclableResource
{
	VkRenderPass RenderPass = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;

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

		checkVk(vkCreateRenderPass(Device, &RenderPassInfo, nullptr, &RenderPass));
	}

	void Destroy()
	{
		vkDestroyRenderPass(Device, RenderPass, nullptr);
		RenderPass = VK_NULL_HANDLE;
	}
};


void TransitionImage(FCmdBuffer* CmdBuffer, VkImage Image, VkImageLayout SrcLayout, VkAccessFlags SrcMask, VkImageLayout DestLayout, VkAccessFlags DstMask, VkImageAspectFlags AspectMask)
{
	VkImageMemoryBarrier TransferToPresentBarrier;
	MemZero(TransferToPresentBarrier);
	TransferToPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	TransferToPresentBarrier.srcAccessMask = SrcMask;
	TransferToPresentBarrier.dstAccessMask = DstMask;// VK_ACCESS_MEMORY_READ_BIT;
	TransferToPresentBarrier.oldLayout = SrcLayout;//VK_IMAGE_LAYOUT_UNDEFINED;
	TransferToPresentBarrier.newLayout = DestLayout;//VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	TransferToPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	TransferToPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	TransferToPresentBarrier.image = Image;
	TransferToPresentBarrier.subresourceRange.aspectMask = AspectMask;// VK_IMAGE_ASPECT_COLOR_BIT;
	TransferToPresentBarrier.subresourceRange.layerCount = 1;
	TransferToPresentBarrier.subresourceRange.levelCount = 1;
	vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &TransferToPresentBarrier);
}

static bool LoadShaders()
{
	static bool bDoCompile = !false;
	if (bDoCompile)
	{
		// Compile the shaders
		char SDKDir[MAX_PATH];
		::GetEnvironmentVariableA("VULKAN_SDK", SDKDir, MAX_PATH - 1);
		char Glslang[MAX_PATH];
		sprintf_s(Glslang, "%s\\Bin\\glslangValidator.exe", SDKDir);

#define CMD_LINE " -V -r -H -l "
		{
			std::string CompileVS = Glslang;
			CompileVS += CMD_LINE " ../Shaders/Test0.vert";
			if (system(CompileVS.c_str()))
			{
				return false;
			}
		}

		{
			std::string CompilePS = Glslang;
			CompilePS += CMD_LINE  " ../Shaders/Test0.frag";
			if (system(CompilePS.c_str()))
			{
				return false;
			}
		}
#undef CMD_LINE
	}

	if (!GVertexShader.Create("vert.spv", GDevice.Device))
	{
		return false;
	}

	if (!GPixelShader.Create("frag.spv", GDevice.Device))
	{
		return false;
	}

	return true;
}

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height)
{
	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice();

	GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	GCmdBufferMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);

	{
		// Setup on Present layout
		auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
		CmdBuffer->Begin();
		GSwapchain.ClearAndTransitionToPresent(CmdBuffer);
		CmdBuffer->End();
		GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
		CmdBuffer->WaitForFence();
	}

	if (!LoadShaders())
	{
		return false;
	}

	GMemMgr.Create(GDevice.Device, GDevice.PhysicalDevice);

	{
		GVB.Create(GDevice.Device);
		auto MemReqs = GVB.GetMemReqs();
		auto MemAlloc = GMemMgr.Alloc(MemReqs);
		vkBindBufferMemory(GDevice.Device, GVB.Buffer, MemAlloc->Mem, 0);

		void* Data;
		checkVk(vkMapMemory(GDevice.Device, MemAlloc->Mem, 0, MemReqs.size, 0, &Data));
		auto* Vertex = (FVertex*)Data;
		Vertex[0].x = -1; Vertex[0].y = -1; Vertex[0].z = 0; Vertex[0].Color = 0xffff0000;
		Vertex[1].x = 1; Vertex[1].y = -1; Vertex[1].z = 0; Vertex[1].Color = 0xff00ff00;
		Vertex[2].x = 0; Vertex[2].y = 1; Vertex[2].z = 0; Vertex[2].Color = 0xff0000ff;
		vkUnmapMemory(GDevice.Device, MemAlloc->Mem);
	}

#if 0
	CreateDescriptorLayouts();
#endif

	return true;
}

FRenderPass GRenderPass;
std::map<int32, FFramebuffer*> GFramebuffers;

void DoRender()
{
	auto* CmdBuffer = GCmdBufferMgr.GetActiveCmdBuffer();
	CmdBuffer->Begin();

	GSwapchain.AcquireNextImage();

	TransitionImage(CmdBuffer, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		static bool b = false;
		if (!b)
		{
			GRenderPass.Create(GDevice.Device);

			GGfxPipeline.Create(GDevice.Device, &GVertexShader, &GPixelShader, GSwapchain.SurfaceResolution.width, GSwapchain.SurfaceResolution.height, GRenderPass.RenderPass);
			for (uint32 Index = 0; Index < GSwapchain.Images.size(); ++Index)
			{
				GFramebuffers[Index] = new FFramebuffer;
				GFramebuffers[Index]->CreateColorOnly(GDevice.Device, GRenderPass.RenderPass, GSwapchain.ImageViews[Index].ImageView, GSwapchain.SurfaceResolution.width, GSwapchain.SurfaceResolution.height);
			}
			b = true;
		}
	}


	CmdBuffer->BeginRenderPass(GRenderPass.RenderPass, *GFramebuffers[GSwapchain.AcquiredImageIndex]);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GGfxPipeline.Pipeline);
	{
		VkViewport Viewport;
		MemZero(Viewport);
		Viewport.width = (float)GSwapchain.SurfaceResolution.width;
		Viewport.height = (float)GSwapchain.SurfaceResolution.height;
		Viewport.maxDepth = 1;
		vkCmdSetViewport(CmdBuffer->CmdBuffer, 0, 1, &Viewport);

		VkRect2D Scissor;
		MemZero(Scissor);
		Scissor.extent.width = GSwapchain.SurfaceResolution.width;
		Scissor.extent.height = GSwapchain.SurfaceResolution.height;
		vkCmdSetScissor(CmdBuffer->CmdBuffer, 0, 1, &Scissor);
	}

	{
		VkDeviceSize Offset = 0;
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GVB.Buffer, &Offset);
		vkCmdDraw(CmdBuffer->CmdBuffer, 3, 1, 0, 0);
	}

	CmdBuffer->EndRenderPass();

	TransitionImage(CmdBuffer, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer->End();

	// First submit needs to wait for present semaphore
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, &GSwapchain.PresentCompleteSemaphores[GSwapchain.PresentCompleteSemaphoreIndex], &GSwapchain.RenderingSemaphores[GSwapchain.AcquiredImageIndex]);

	GSwapchain.Present(GDevice.PresentQueue);
}

void DoResize(uint32 Width, uint32 Height)
{
	if (Width != GSwapchain.SurfaceResolution.width && Height != GSwapchain.SurfaceResolution.height)
	{
		GSwapchain.Destroy();
		GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);
	}
}

void DoDeinit()
{
	GCmdBufferMgr.Destroy();

	for (auto& Pair : GFramebuffers)
	{
		Pair.second->Destroy();
		delete Pair.second;
	}

	GRenderPass.Destroy();

	GVB.Destroy(GDevice.Device);

	GGfxPipeline.Destroy(GDevice.Device);
	GPixelShader.Destroy(GDevice.Device);
	GVertexShader.Destroy(GDevice.Device);

	GSwapchain.Destroy();

#if 0
	GResourceRecycler.Deinit();
#endif
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
