// Vk.cpp

#include "stdafx.h"
#include "Vk.h"
#include "VkMem.h"
#include "VkResources.h"
#include "../Meshes/ObjLoader.h"

struct FCmdBuffer;

static FBuffer GObjVB;
static Obj::FObj GObj;
FDescriptorPool GDescriptorPool;
FBuffer GTriVB;

struct FViewUB
{
	FMatrix4x4 View;
	FMatrix4x4 Proj;
};
FBuffer GViewUB;

struct FObjUB
{
	FMatrix4x4 Obj;
};
FBuffer GObjUB;

FImage GImage;
FImage GDepthImage;
FImageView GImageView;
FImageView GDepthImageView;
FSampler GSampler;


bool GQuitting = false;

static bool GSkipValidation = false;

void ImageBarrier(FCmdBuffer* CmdBuffer, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DestStage, VkImage Image, VkImageLayout SrcLayout, VkAccessFlags SrcMask, VkImageLayout DestLayout, VkAccessFlags DstMask, VkImageAspectFlags AspectMask);

static void GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
{
	if (!GSkipValidation)
	{
		uint32 NumLayers;
		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));

		std::vector<VkLayerProperties> InstanceProperties;
		InstanceProperties.resize(NumLayers);

		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, &InstanceProperties[0]));

		const char* ValidationLayers[] =
		{
			"VK_LAYER_LUNARG_standard_validation",
			"VK_LAYER_LUNARG_image",
			"VK_LAYER_LUNARG_object_tracker",
			"VK_LAYER_LUNARG_parameter_validation",
			"VK_LAYER_LUNARG_screenshot",
			"VK_LAYER_LUNARG_swapchain",
			"VK_LAYER_GOOGLE_threading",
			"VK_LAYER_GOOGLE_unique_objects",
		};

		for (auto* DesiredLayer : ValidationLayers)
		{
			for (auto& Prop : InstanceProperties)
			{
				if (!strcmp(Prop.layerName, DesiredLayer))
				{
					OutLayers.push_back(DesiredLayer);
					// Should probably remove it from InstanceProperties array...
					break;
				}
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


FMatrix4x4 CalculateProjectionMatrix(float FOVRadians, float Aspect, float NearZ, float FarZ)
{
	const float HalfTanFOV = (float)tan(FOVRadians / 2.0);
	FMatrix4x4 New = FMatrix4x4::GetZero();
	New.Set(0, 0, 1.0f / (Aspect * HalfTanFOV));
	New.Set(1, 1, 1.0f / HalfTanFOV);
	New.Set(2, 3, -1);
	New.Set(2, 2, FarZ / (NearZ - FarZ));
	New.Set(3, 2, -(FarZ * NearZ) / (FarZ - NearZ));
	return New;
}


struct FTestPSO : public FGfxPSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		VkDescriptorSetLayoutBinding Binding;
		MemZero(Binding);
		Binding.binding = 0;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		OutBindings.push_back(Binding);

		MemZero(Binding);
		Binding.binding = 1;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		OutBindings.push_back(Binding);

		MemZero(Binding);
		Binding.binding = 2;
		//Binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		OutBindings.push_back(Binding);
	}

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages)
	{
		VkPipelineShaderStageCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		Info.stage = VK_SHADER_STAGE_VERTEX_BIT;
		Info.module = VS.ShaderModule;
		Info.pName = "main";
		OutShaderStages.push_back(Info);

		if (PS.ShaderModule != VK_NULL_HANDLE)
		{
			MemZero(Info);
			Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			Info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			Info.module = PS.ShaderModule;
			Info.pName = "main";
			OutShaderStages.push_back(Info);
		}
	}
};
FTestPSO GTestPSO;

struct FTestComputePSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
		VkDescriptorSetLayoutBinding Binding;
		MemZero(Binding);
		Binding.binding = 0;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		OutBindings.push_back(Binding);
	}

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages)
	{
		VkPipelineShaderStageCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		Info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		Info.module = CS.ShaderModule;
		Info.pName = "main";
		OutShaderStages.push_back(Info);
	}
};
FTestComputePSO GTestComputePSO;

struct FTestPostComputePSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
		VkDescriptorSetLayoutBinding Binding;
		MemZero(Binding);
		Binding.binding = 0;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		OutBindings.push_back(Binding);

		MemZero(Binding);
		Binding.binding = 1;
		Binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		Binding.descriptorCount = 1;
		Binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		OutBindings.push_back(Binding);
	}

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages)
	{
		VkPipelineShaderStageCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		Info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		Info.module = CS.ShaderModule;
		Info.pName = "main";
		OutShaderStages.push_back(Info);
	}
};
FTestPostComputePSO GTestComputePostPSO;

struct FVertex
{
	float x, y, z;
	uint32 Color;
	float u, v;
};


struct FGfxPipeline : public FBasePipeline
{
	void Create(VkDevice Device, FGfxPSO* PSO, uint32 Width, uint32 Height, VkRenderPass RenderPass)
	{
		std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
		PSO->SetupShaderStages(ShaderStages);

		VkPipelineLayoutCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		CreateInfo.setLayoutCount = 1;
		CreateInfo.pSetLayouts = &PSO->DSLayout;
		checkVk(vkCreatePipelineLayout(Device, &CreateInfo, nullptr, &PipelineLayout));

		VkVertexInputBindingDescription VBDesc;
		MemZero(VBDesc);
		VBDesc.binding = 0;
		VBDesc.stride = sizeof(FVertex);
		VBDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription VIADesc[3];
		MemZero(VIADesc);
		//VIADesc.location = 0;
		VIADesc[0].binding = 0;
		VIADesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		VIADesc[0].offset = offsetof(FVertex, x);
		//VIADesc[1].binding = 0;
		VIADesc[1].location = 1;
		VIADesc[1].format = VK_FORMAT_R8G8B8A8_UNORM;
		VIADesc[1].offset = offsetof(FVertex, Color);
		VIADesc[2].location = 2;
		VIADesc[2].format = VK_FORMAT_R32G32_SFLOAT;
		VIADesc[2].offset = offsetof(FVertex, u);

		VkPipelineVertexInputStateCreateInfo VIInfo;
		MemZero(VIInfo);
		VIInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VIInfo.vertexBindingDescriptionCount = 1;
		VIInfo.pVertexBindingDescriptions = &VBDesc;
		VIInfo.vertexAttributeDescriptionCount = ARRAYSIZE(VIADesc);
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
		PipelineInfo.stageCount = ShaderStages.size();
		PipelineInfo.pStages = &ShaderStages[0];
		PipelineInfo.pVertexInputState = &VIInfo;
		PipelineInfo.pInputAssemblyState = &IAInfo;
		//PipelineInfo.pTessellationState = NULL;
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
};

static FGfxPipeline GGfxPipeline;


struct FComputePipeline : public FBasePipeline
{
	void Create(VkDevice Device, FComputePSO* PSO)
	{
		std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
		PSO->SetupShaderStages(ShaderStages);
		check(ShaderStages.size() == 1);

		VkPipelineLayoutCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		CreateInfo.setLayoutCount = 1;
		CreateInfo.pSetLayouts = &PSO->DSLayout;
		checkVk(vkCreatePipelineLayout(Device, &CreateInfo, nullptr, &PipelineLayout));

		VkComputePipelineCreateInfo PipelineInfo;
		MemZero(PipelineInfo);
		PipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		//VkPipelineCreateFlags              flags;
		PipelineInfo.stage = ShaderStages[0];
		PipelineInfo.layout = PipelineLayout;
		//VkPipeline                         basePipelineHandle;
		//int32_t                            basePipelineIndex;
		checkVk(vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline));
	}
};
static FComputePipeline GComputePipeline;
static FComputePipeline GComputePostPipeline;

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
		int n = 0;
		if (Flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
		{
			char s[2048];
			sprintf_s(s, "<VK>Error: %s\n", Message);
			::OutputDebugStringA(s);
			check(0);
			++n;
		}
		else if (Flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
		{
			char s[2048];
			sprintf_s(s, "<VK>Warn: %s\n", Message);
			::OutputDebugStringA(s);
			++n;
		}
		else if (1)
		{
			static const char* SkipPrefixes[] =
			{
				"OBJTRACK",
				"loader",
				"MEM",
				"DS",
			};
			for (uint32 Index = 0; Index < ARRAYSIZE(SkipPrefixes); ++Index)
			{
				if (!strcmp(LayerPrefix, SkipPrefixes[Index]))
				{
					return false;
				}
			}

			uint32 Size = strlen(Message) + 100;
			auto* s = new char[Size];
			snprintf(s, Size - 1, "<VK>: %s\n", Message);
			::OutputDebugStringA(s);
			delete[] s;
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



FMemPage::FMemPage(VkDevice InDevice, VkDeviceSize Size, uint32 InMemTypeIndex, bool bInMapped)
	: Allocation(InDevice, Size, MemTypeIndex, bInMapped)
	, MemTypeIndex(InMemTypeIndex)
{
	FRange Block;
	Block.Begin = 0;
	Block.End = Size;
	FreeList.push_back(Block);
}

FMemPage::~FMemPage()
{
	check(FreeList.size() == 1);
	check(SubAllocations.empty());
	Allocation.Destroy();
}

FMemSubAlloc* FMemPage::TryAlloc(uint64 Size, uint64 Alignment)
{
	for (auto& Range : FreeList)
	{
		uint64 AlignedOffset = Align(Range.Begin, Alignment);
		if (AlignedOffset + Size <= Range.End)
		{
			auto* SubAlloc = new FMemSubAlloc(Range.Begin, AlignedOffset, Size, this);
			SubAllocations.push_back(SubAlloc);
			Range.Begin = AlignedOffset + Size;
			return SubAlloc;
		}
	}

	return nullptr;
}

void FMemPage::Release(FMemSubAlloc* SubAlloc)
{
	FRange NewRange;
	NewRange.Begin = SubAlloc->AllocatedOffset;
	NewRange.End = SubAlloc->AllocatedOffset + SubAlloc->Size;
	FreeList.push_back(NewRange);
	SubAllocations.remove(SubAlloc);
	delete SubAlloc;

	{
		std::sort(FreeList.begin(), FreeList.end(),
			[](const FRange& Left, const FRange& Right)
		{
			return Left.Begin < Right.Begin;
		});

		for (uint32 Index = FreeList.size() - 1; Index > 0; --Index)
		{
			auto& Current = FreeList[Index];
			auto& Prev = FreeList[Index - 1];
			if (Current.Begin == Prev.End)
			{
				Prev.End = Current.End;
				for (uint32 SubIndex = Index; SubIndex < FreeList.size() - 1; ++SubIndex)
				{
					FreeList[SubIndex] = FreeList[SubIndex + 1];
				}
				FreeList.resize(FreeList.size() - 1);
			}
		}
	}
}


FMemManager GMemMgr;


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
		VkClearColorValue Color;
		MemZero(Color);
		VkImageSubresourceRange Range;
		MemZero(Range);
		Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Range.levelCount = 1;
		Range.layerCount = 1;
		for (uint32 Index = 0; Index < (uint32)Images.size(); ++Index)
		{
			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Images[Index], VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
			vkCmdClearColorImage(CmdBuffer->CmdBuffer, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
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


void FCmdBuffer::BeginRenderPass(VkRenderPass RenderPass, const FFramebuffer& Framebuffer)
{
	check(State == EState::Beginned);

	static uint32 N = 0;
	N = (N + 1) % 256;

	VkClearValue ClearValues[2] = { { N / 255.0f, 1.0f, 0.0f, 1.0f },{ 1.0, 0.0 } };
	ClearValues[1].depthStencil.depth = 1.0f;
	ClearValues[1].depthStencil.stencil = 0;
	VkRenderPassBeginInfo BeginInfo = {};
	BeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	BeginInfo.renderPass = RenderPass;
	BeginInfo.framebuffer = Framebuffer.Framebuffer;
	BeginInfo.renderArea = { 0, 0, Framebuffer.Width, Framebuffer.Height };
	BeginInfo.clearValueCount = 2;
	BeginInfo.pClearValues = ClearValues;
	vkCmdBeginRenderPass(CmdBuffer, &BeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	State = EState::InsideRenderPass;
}

void ImageBarrier(FCmdBuffer* CmdBuffer, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DestStage, VkImage Image, VkImageLayout SrcLayout, VkAccessFlags SrcMask, VkImageLayout DestLayout, VkAccessFlags DstMask, VkImageAspectFlags AspectMask)
{
	VkImageMemoryBarrier TransferToPresentBarrier;
	MemZero(TransferToPresentBarrier);
	TransferToPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	TransferToPresentBarrier.srcAccessMask = SrcMask;
	TransferToPresentBarrier.dstAccessMask = DstMask;
	TransferToPresentBarrier.oldLayout = SrcLayout;
	TransferToPresentBarrier.newLayout = DestLayout;
	TransferToPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	TransferToPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	TransferToPresentBarrier.image = Image;
	TransferToPresentBarrier.subresourceRange.aspectMask = AspectMask;;
	TransferToPresentBarrier.subresourceRange.layerCount = 1;
	TransferToPresentBarrier.subresourceRange.levelCount = 1;
	vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, SrcStage, DestStage, 0, 0, nullptr, 0, nullptr, 1, &TransferToPresentBarrier);
}

struct FResizableObjects
{
	FRenderPass RenderPass;
	std::map<int32, FFramebuffer*> Framebuffers;

	void Create()
	{
		RenderPass.Create(GDevice.Device);

		GGfxPipeline.Create(GDevice.Device, &GTestPSO, GSwapchain.SurfaceResolution.width, GSwapchain.SurfaceResolution.height, RenderPass.RenderPass);
		GComputePipeline.Create(GDevice.Device, &GTestComputePSO);
		GComputePostPipeline.Create(GDevice.Device, &GTestComputePostPSO);
		for (uint32 Index = 0; Index < GSwapchain.Images.size(); ++Index)
		{
			Framebuffers[Index] = new FFramebuffer;
			Framebuffers[Index]->CreateColorAndDepth(GDevice.Device, RenderPass.RenderPass, GSwapchain.ImageViews[Index].ImageView, GDepthImageView.ImageView, GSwapchain.SurfaceResolution.width, GSwapchain.SurfaceResolution.height);
		}
	}

	void Destroy()
	{
		for (auto& Pair : Framebuffers)
		{
			Pair.second->Destroy();
			delete Pair.second;
		}

		RenderPass.Destroy();

		GComputePostPipeline.Destroy(GDevice.Device);
		GComputePipeline.Destroy(GDevice.Device);
		GGfxPipeline.Destroy(GDevice.Device);
	}
};
FResizableObjects GResizableObjects;


template <typename TFillLambda>
void MapAndFillBufferSync(FBuffer& StagingBuffer, FCmdBuffer* CmdBuffer, FBuffer* DestBuffer, TFillLambda Fill, uint32 Size)
{
	StagingBuffer.Create(GDevice.Device, Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);
	void* Data = StagingBuffer.GetMappedData();
	check(Data);
	auto* Vertex = (FVertex*)Data;
	Fill(Vertex);

	{
		VkBufferCopy Region;
		MemZero(Region);
		Region.srcOffset = StagingBuffer.GetBindOffset();
		Region.size = StagingBuffer.GetSize();
		Region.dstOffset = DestBuffer->GetBindOffset();
		vkCmdCopyBuffer(CmdBuffer->CmdBuffer, StagingBuffer.Buffer, DestBuffer->Buffer, 1, &Region);
	}
}

template <typename TFillLambda>
void MapAndFillBufferSync(FBuffer* DestBuffer, TFillLambda Fill, uint32 Size)
{
	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();
	FBuffer StagingBuffer;
	MapAndFillBufferSync(StagingBuffer, CmdBuffer, DestBuffer, Fill, Size);

	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();

	StagingBuffer.Destroy(GDevice.Device);
}

static bool LoadShadersAndGeometry()
{
	static bool bDoCompile = !false;
	if (bDoCompile)
	{
		// Compile the shaders
		char SDKDir[MAX_PATH];
		::GetEnvironmentVariableA("VULKAN_SDK", SDKDir, MAX_PATH - 1);
		char Glslang[MAX_PATH];
		sprintf_s(Glslang, "%s\\Bin\\glslangValidator.exe", SDKDir);

		auto DoCompile = [&](const char* InFile)
		{
			std::string Compile = Glslang;
			Compile += " -V -r -H -l ";
			Compile += InFile;
			if (system(Compile.c_str()))
			{
				return false;
			}

			return true;
		};

		if (!DoCompile(" ../Shaders/Test0.vert"))
		{
			return false;
		}

		if (!DoCompile(" ../Shaders/Test0.frag"))
		{
			return false;
		}

		if (!DoCompile(" ../Shaders/Test0.comp"))
		{
			return false;
		}

		if (!DoCompile(" -o TestPost.spv ../Shaders/TestPost.comp"))
		{
			return false;
		}
#undef CMD_LINE
	}

	check(GTestPSO.CreateVSPS(GDevice.Device, "vert.spv", "frag.spv"));
	check(GTestComputePSO.Create(GDevice.Device, "comp.spv"));
	check(GTestComputePostPSO.Create(GDevice.Device, "TestPost.spv"));

	if (!Obj::Load("../Meshes/Cube/cube.obj", GObj))
	{
		return false;
	}
	//GObj.Faces.resize(1);
	GObjVB.Create(GDevice.Device, sizeof(FVertex) * GObj.Faces.size() * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);

	auto FillObj = [](void* Data)
	{
		check(Data);
		auto* Vertex = (FVertex*)Data;
		for (uint32 Index = 0; Index < GObj.Faces.size(); ++Index)
		{
			auto& Face = GObj.Faces[Index];
			for (uint32 Corner = 0; Corner < 3; ++Corner)
			{
				Vertex->x = GObj.Vs[Face.Corners[Corner].Pos].x;
				Vertex->y = GObj.Vs[Face.Corners[Corner].Pos].y;
				Vertex->z = GObj.Vs[Face.Corners[Corner].Pos].z;
				Vertex->Color = PackNormalToU32(GObj.VNs[Face.Corners[Corner].Normal]);
				Vertex->u = GObj.VTs[Face.Corners[Corner].UV].u;
				Vertex->v = GObj.VTs[Face.Corners[Corner].UV].v;
				++Vertex;
			}
		}
	};

	MapAndFillBufferSync(&GObjVB, FillObj, sizeof(FVertex) * GObj.Faces.size() * 3);

	return true;
}

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height)
{
	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice();

	GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	GCmdBufferMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);

	GMemMgr.Create(GDevice.Device, GDevice.PhysicalDevice);

	GDescriptorPool.Create(GDevice.Device);

	if (!LoadShadersAndGeometry())
	{
		return false;
	}

	GTriVB.Create(GDevice.Device, sizeof(FVertex) * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	auto FillTri = [](void* Data)
	{
		check(Data);
		auto* Vertex = (FVertex*)Data;
		Vertex[0].x = -0.5f; Vertex[0].y = -0.5f; Vertex[0].z = -1; Vertex[0].Color = 0xffff0000;
		Vertex[1].x = 0.1f; Vertex[1].y = -0.5f; Vertex[1].z = -1; Vertex[1].Color = 0xff00ff00;
		Vertex[2].x = 0; Vertex[2].y = 0.5f; Vertex[2].z = -1; Vertex[2].Color = 0xff0000ff;
	};
	MapAndFillBufferSync(&GTriVB, FillTri, sizeof(FVertex) * 3);

	GViewUB.Create(GDevice.Device, sizeof(FViewUB), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);
	GObjUB.Create(GDevice.Device, sizeof(FObjUB), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);

	FViewUB& ViewUB = *(FViewUB*)GViewUB.GetMappedData();
	ViewUB.View = FMatrix4x4::GetIdentity();
	ViewUB.View.Values[3 * 4 + 2] = -2;
	ViewUB.Proj = CalculateProjectionMatrix(ToRadians(60), (float)GSwapchain.SurfaceResolution.width / (float)GSwapchain.SurfaceResolution.height, 0.1f, 1000.0f);

	FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
	ObjUB.Obj = FMatrix4x4::GetIdentity();

	GImage.Create(GDevice.Device, 16, 16, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	GImageView.Create(GDevice.Device, GImage.Image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
	GSampler.Create(GDevice.Device);
	GDepthImage.Create(GDevice.Device, GSwapchain.SurfaceResolution.width, GSwapchain.SurfaceResolution.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	GDepthImageView.Create(GDevice.Device, GDepthImage.Image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

	GResizableObjects.Create();

	FBuffer StagingBuffer;
	{
		// Setup on Present layout
		auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
		CmdBuffer->Begin();
		GSwapchain.ClearAndTransitionToPresent(CmdBuffer);
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GDepthImage.Image, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
		{
			// Fill texture
			VkClearColorValue Color;
			Color.float32[0] = 1.0f;	// B
			Color.float32[1] = 0.0f;	// G
			Color.float32[2] = 1.0f;	// R
			Color.float32[3] = 0.0f;
			VkImageSubresourceRange Range;
			MemZero(Range);
			Range.layerCount = 1;
			Range.levelCount = 1;
			Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GImage.Image, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
			vkCmdClearColorImage(CmdBuffer->CmdBuffer, GImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);

			StagingBuffer.Create(GDevice.Device, GImage.Reqs.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);
			uint32* P = (uint32*)StagingBuffer.GetMappedData();
			for (uint32 y = 0; y < GImage.Height; ++y)
			{
				for (uint32 x = 0; x < GImage.Width; ++x)
				{
					uint8 Byte = uint8(max(y / (float)GImage.Height, x / (float)GImage.Width) * 255.0f);
					*P = Byte | (Byte << 8) | (Byte << 16);
					++P;
				}
			}
			VkBufferImageCopy Region;
			MemZero(Region);
			Region.bufferOffset = StagingBuffer.GetBindOffset();
			Region.bufferRowLength = 0;// GImage.Width;
			Region.bufferImageHeight = 0;// GImage.Height;
			Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Region.imageSubresource.layerCount = 1;
			Region.imageExtent.width = GImage.Width;
			Region.imageExtent.height = GImage.Height;
			Region.imageExtent.depth = 1;
			vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, StagingBuffer.Buffer, GImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		}
		CmdBuffer->End();
		GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
		CmdBuffer->WaitForFence();
	}
	StagingBuffer.Destroy(GDevice.Device);

	return true;
}

void TestCompute(FCmdBuffer* CmdBuffer)
{
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GComputePipeline.Pipeline);

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GImage.Image, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestComputePSO.DSLayout);

		VkDescriptorImageInfo ImageInfo;
		MemZero(ImageInfo);
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageInfo.imageView = GImageView.ImageView;
		//ImageInfo.sampler = GSampler.Sampler;

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescriptorSet;
		DSWrite.dstBinding = 0;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		DSWrite.pImageInfo = &ImageInfo;
		vkUpdateDescriptorSets(GDevice.Device, 1, &DSWrite, 0, nullptr);
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GComputePipeline.PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, GImage.Width / 8, GImage.Height / 8, 1);

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GImage.Image, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, GComputePostPipeline.Pipeline);

	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestComputePostPSO.DSLayout);

		VkDescriptorImageInfo ImageInfo;
		MemZero(ImageInfo);
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageInfo.imageView = GImageView.ImageView;
		//ImageInfo.sampler = GSampler.Sampler;

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescriptorSet;
		DSWrite.dstBinding = 0;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		DSWrite.pImageInfo = &ImageInfo;
		vkUpdateDescriptorSets(GDevice.Device, 1, &DSWrite, 0, nullptr);
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GComputePostPipeline.PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, GImage.Width / 8, GImage.Height / 8, 1);

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GImage.Image, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void DoRender()
{
	if (GQuitting)
	{
		return;
	}
	auto* CmdBuffer = GCmdBufferMgr.GetActiveCmdBuffer();
	CmdBuffer->Begin();

	GSwapchain.AcquireNextImage();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	TestCompute(CmdBuffer);

	CmdBuffer->BeginRenderPass(GResizableObjects.RenderPass.RenderPass, *GResizableObjects.Framebuffers[GSwapchain.AcquiredImageIndex]);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GGfxPipeline.Pipeline);

	FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
	static float AngleDegrees = 0;
	{
		AngleDegrees += 360.0f / 10.0f / 60.0f;
		AngleDegrees = fmod(AngleDegrees, 360.0f);
	}
	ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees));

	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

		std::vector<VkWriteDescriptorSet> DSWrites;
		std::vector<VkDescriptorBufferInfo> BufferInfos;
		std::vector<VkDescriptorImageInfo> ImageInfos;
		{
			VkDescriptorBufferInfo BufferInfo;
			MemZero(BufferInfo);
			BufferInfo.buffer = GViewUB.Buffer;
			BufferInfo.offset = GViewUB.GetBindOffset();
			BufferInfo.range = GViewUB.GetSize();
			BufferInfos.push_back(BufferInfo);

			MemZero(BufferInfo);
			BufferInfo.buffer = GObjUB.Buffer;
			BufferInfo.offset = GObjUB.GetBindOffset();
			BufferInfo.range = GObjUB.GetSize();
			BufferInfos.push_back(BufferInfo);

			VkWriteDescriptorSet DSWrite;
			MemZero(DSWrite);
			DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			DSWrite.dstSet = DescriptorSet;
			DSWrite.dstBinding = 0;
			DSWrite.descriptorCount = 1;
			DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			DSWrite.pBufferInfo = &BufferInfos[0];
			DSWrites.push_back(DSWrite);

			MemZero(DSWrite);
			DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			DSWrite.dstSet = DescriptorSet;
			DSWrite.dstBinding = 1;
			DSWrite.descriptorCount = 1;
			DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			DSWrite.pBufferInfo = &BufferInfos[1];
			DSWrites.push_back(DSWrite);

			VkDescriptorImageInfo ImageInfo;
			MemZero(ImageInfo);
			ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			ImageInfo.imageView = GImageView.ImageView;
			ImageInfo.sampler = GSampler.Sampler;
			ImageInfos.push_back(ImageInfo);

			MemZero(DSWrite);
			DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			DSWrite.dstSet = DescriptorSet;
			DSWrite.dstBinding = 2;
			DSWrite.descriptorCount = 1;
			//DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			DSWrite.pImageInfo = &ImageInfos[0];
			DSWrites.push_back(DSWrite);
		}

		vkUpdateDescriptorSets(GDevice.Device, DSWrites.size(), &DSWrites[0], 0, nullptr);
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GGfxPipeline.PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}
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
		//vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GVB.Buffer, &Offset);
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GObjVB.Buffer, &Offset);
		vkCmdDraw(CmdBuffer->CmdBuffer, GObj.Faces.size() * 3, 1, 0, 0);
	}

	CmdBuffer->EndRenderPass();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer->End();

	// First submit needs to wait for present semaphore
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, &GSwapchain.PresentCompleteSemaphores[GSwapchain.PresentCompleteSemaphoreIndex], &GSwapchain.RenderingSemaphores[GSwapchain.AcquiredImageIndex]);

	GSwapchain.Present(GDevice.PresentQueue);
}

void DoResize(uint32 Width, uint32 Height)
{
	if (Width != GSwapchain.SurfaceResolution.width && Height != GSwapchain.SurfaceResolution.height)
	{
		vkDeviceWaitIdle(GDevice.Device);
		GSwapchain.Destroy();
		GResizableObjects.Destroy();
		GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);
		GResizableObjects.Create();
	}
}

void DoDeinit()
{
	GQuitting = true;

	checkVk(vkDeviceWaitIdle(GDevice.Device));

	GResizableObjects.Destroy();
	GCmdBufferMgr.Destroy();

	GTriVB.Destroy(GDevice.Device);
	GViewUB.Destroy(GDevice.Device);
	GObjUB.Destroy(GDevice.Device);
	GObjVB.Destroy(GDevice.Device);

	GImage.Destroy(GDevice.Device);
	GSampler.Destroy();
	GImageView.Destroy();

	GDepthImageView.Destroy();
	GDepthImage.Destroy(GDevice.Device);

	GDescriptorPool.Destroy();

	GTestComputePostPSO.Destroy(GDevice.Device);
	GTestComputePSO.Destroy(GDevice.Device);
	GTestPSO.Destroy(GDevice.Device);

	GSwapchain.Destroy();

#if 0
	GResourceRecycler.Deinit();
#endif
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
