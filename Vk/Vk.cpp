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

FImage2DWithView GCheckerboardTexture;
FImage2DWithView GSceneColor;
FImage2DWithView GSceneColorAfterPost;
FImage2DWithView GDepthBuffer;
FImage2DWithView GHeightMap;
FSampler GSampler;


bool GQuitting = false;

static bool GSkipValidation = false;

void FInstance::GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
{
	if (!GSkipValidation)
	{
		uint32 NumLayers;
		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));

		std::vector<VkLayerProperties> InstanceProperties;
		InstanceProperties.resize(NumLayers);

		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, &InstanceProperties[0]));

		const char* UseValidationLayers[] =
		{
			"VK_LAYER_LUNARG_api_dump",
			"VK_LAYER_LUNARG_standard_validation",
			"VK_LAYER_LUNARG_image",
			"VK_LAYER_LUNARG_object_tracker",
			"VK_LAYER_LUNARG_parameter_validation",
			"VK_LAYER_LUNARG_screenshot",
			"VK_LAYER_LUNARG_swapchain",
			"VK_LAYER_GOOGLE_threading",
			"VK_LAYER_GOOGLE_unique_objects",
		};

		for (auto* DesiredLayer : UseValidationLayers)
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

		const char* UseExtensions[] =
		{
			"VK_KHR_surface",
			"VK_KHR_win32_surface",
			"VK_EXT_debug_report",
		};
		for (auto* DesiredExtension : UseExtensions)
		{
			for (auto& Extension : ExtensionsProperties)
			{
				if (!strcmp(Extension.extensionName, DesiredExtension))
				{
					OutExtensions.push_back(DesiredExtension);
					// Should probably remove it from ExtensionsProperties array...
					break;
				}
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
		AddBinding(OutBindings, VK_SHADER_STAGE_VERTEX_BIT, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, VK_SHADER_STAGE_VERTEX_BIT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, VK_SHADER_STAGE_FRAGMENT_BIT, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	}
};
FTestPSO GTestPSO;

struct FOneImagePSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FOneImagePSO GFillTexturePSO;

struct FTwoImagesPSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FTwoImagesPSO GTestComputePSO;

struct FTestPostComputePSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
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

void FInstance::CreateDevice(FDevice& OutDevice)
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
				OutDevice.PhysicalDevice = Devices[Index];
				OutDevice.DeviceProperties = DeviceProperties;
				OutDevice.PresentQueueFamilyIndex = QueueIndex;
				goto Found;
			}
		}
	}

	// Not found!
	check(0);
	return;

Found:
	OutDevice.Create(Layers);
}
FDevice GDevice;

FCmdBufferMgr GCmdBufferMgr;

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


FSwapchain GSwapchain;


void FCmdBuffer::BeginRenderPass(VkRenderPass RenderPass, const FFramebuffer& Framebuffer)
{
	check(State == EState::Begun);

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

struct FObjectCache
{
	FDevice* Device = nullptr;

	std::map<uint64, FRenderPass*> RenderPasses;
	std::map<FComputePSO*, FComputePipeline*> ComputePipelines;
	std::map<FGfxPSOLayout, FGfxPipeline*> GfxPipelines;

	struct FFrameBufferEntry
	{
		FFramebuffer* Framebuffer;

		FFrameBufferEntry(VkRenderPass InRenderPass, uint32 InWidth, uint32 InHeight, uint32 InNumColorTargets, VkImageView* InColorViews, VkImageView InDepthStencilView = VK_NULL_HANDLE)
			: RenderPass(InRenderPass)
			, Width(InWidth)
			, Height(InHeight)
			, NumColorTargets(InNumColorTargets)
			, DepthStencilView(InDepthStencilView)
		{
			for (uint32 Index = 0; Index < InNumColorTargets; ++Index)
			{
				ColorViews[Index] = InColorViews[Index];
			}
		}

		VkRenderPass RenderPass = VK_NULL_HANDLE;
		uint32 Width = 0;
		uint32 Height = 0;
		uint32 NumColorTargets = 0;
		VkImageView ColorViews[FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
		VkImageView DepthStencilView = VK_NULL_HANDLE;
	};
	std::vector<FFrameBufferEntry> Framebuffers;

	void Create(FDevice* InDevice)
	{
		Device = InDevice;
	}

	FFramebuffer* GetOrCreateFramebuffer(VkRenderPass RenderPass, VkImageView Color, VkImageView DepthStencil, uint32 Width, uint32 Height)
	{
		for (auto& Entry : Framebuffers)
		{
			if (Entry.RenderPass == RenderPass && Entry.NumColorTargets == 1 && Entry.ColorViews[0] == Color && Entry.DepthStencilView == DepthStencil && Entry.Width == Width && Entry.Height == Height)
			{
				return Entry.Framebuffer;
			}
		}

		FFrameBufferEntry Entry(RenderPass, Width, Height, 1, &Color, DepthStencil);

		auto* NewFramebuffer = new FFramebuffer;
		NewFramebuffer->Create(Device->Device, RenderPass, Color, DepthStencil, Width, Height);
		Entry.Framebuffer = NewFramebuffer;
		Framebuffers.push_back(Entry);
		return NewFramebuffer;
	}

	FGfxPipeline* GetOrCreateGfxPipeline(FGfxPSO* GfxPSO, uint32 Width, uint32 Height, VkRenderPass RenderPass)
	{
		FGfxPSOLayout Layout(GfxPSO, Width, Height, RenderPass);
		auto Found = GfxPipelines.find(Layout);
		if (Found != GfxPipelines.end())
		{
			return Found->second;
		}

		auto* NewPipeline = new FGfxPipeline;
		NewPipeline->Create(Device->Device, GfxPSO, Width, Height, RenderPass);
		GfxPipelines[Layout] = NewPipeline;
		return NewPipeline;
	}

	FComputePipeline* GetOrCreateComputePipeline(FComputePSO* ComputePSO)
	{
		auto Found = ComputePipelines.find(ComputePSO);
		if (Found != ComputePipelines.end())
		{
			return Found->second;
		}

		auto* NewPipeline = new FComputePipeline;
		NewPipeline->Create(Device->Device, ComputePSO);
		ComputePipelines[ComputePSO] = NewPipeline;
		return NewPipeline;
	}

	FRenderPass* GetOrCreateRenderPass(uint32 Width, uint32 Height, uint32 NumColorTargets, VkFormat* ColorFormats, VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED)
	{
		FRenderPassLayout Layout(Width, Height, NumColorTargets, ColorFormats, DepthStencilFormat);
		auto LayoutHash = Layout.GetHash();
		auto Found = RenderPasses.find(LayoutHash);
		if (Found != RenderPasses.end())
		{
			return Found->second;
		}

		auto* NewRenderPass = new FRenderPass;
		NewRenderPass->Create(Device->Device, Layout);
		RenderPasses[LayoutHash] = NewRenderPass;
		return NewRenderPass;
	}

	void Destroy()
	{
		for (auto& Pair : RenderPasses)
		{
			Pair.second->Destroy();
			delete Pair.second;
		}
		RenderPasses.swap(decltype(RenderPasses)());

		for (auto& Pair : ComputePipelines)
		{
			Pair.second->Destroy(Device->Device);
			delete Pair.second;
		}
		ComputePipelines.swap(decltype(ComputePipelines)());

		for (auto& Pair : GfxPipelines)
		{
			Pair.second->Destroy(Device->Device);
			delete Pair.second;
		}
		GfxPipelines.swap(decltype(GfxPipelines)());

		for (auto& Entry : Framebuffers)
		{
			Entry.Framebuffer->Destroy();
			delete Entry.Framebuffer;
		}
		Framebuffers.swap(decltype(Framebuffers)());
	}
};
FObjectCache GObjectCache;


template <typename TFillLambda>
void MapAndFillBufferSync(FBuffer* DestBuffer, TFillLambda Fill, uint32 Size)
{
	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();
	FBuffer StagingBuffer;
	MapAndFillBufferSync(StagingBuffer, CmdBuffer, DestBuffer, Fill, Size);
	FlushMappedBuffer(GDevice.Device, &StagingBuffer);
	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();

	StagingBuffer.Destroy(GDevice.Device);
}

static bool LoadShadersAndGeometry()
{
	static bool bDoCompile = false;
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
			Compile += " -V -r -H -l -o ";
			Compile += InFile;
			Compile += ".spv ";
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

		if (!DoCompile(" ../Shaders/TestPost.comp"))
		{
			return false;
		}

		if (!DoCompile(" ../Shaders/FillTexture.comp"))
		{
			return false;
		}

		check(GTestPSO.CreateVSPS(GDevice.Device, "vert.spv", "frag.spv"));
		check(GTestComputePSO.Create(GDevice.Device, "comp.spv"));
		check(GTestComputePostPSO.Create(GDevice.Device, "TestPost.spv"));
		check(GFillTexturePSO.Create(GDevice.Device, "FillTexture.spv"));
	}
	else
	{
		check(GTestPSO.CreateVSPS(GDevice.Device, "../Shaders/Test0.vert.spv", "../Shaders/Test0.frag.spv"));
		check(GTestComputePSO.Create(GDevice.Device, "../Shaders/Test0.comp.spv"));
		check(GTestComputePostPSO.Create(GDevice.Device, "../Shaders/TestPost.comp.spv"));
		check(GFillTexturePSO.Create(GDevice.Device, "../Shaders/FillTexture.comp.spv"));
	}

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

void CreateAndFillTexture()
{
	srand(0);
	GCheckerboardTexture.Create(GDevice.Device, 64, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);
	GHeightMap.Create(GDevice.Device, 64, 64, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);

	FComputePipeline* Pipeline = GObjectCache.GetOrCreateComputePipeline(&GFillTexturePSO);

	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GCheckerboardTexture.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Pipeline);
	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GFillTexturePSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddStorageImage(DescriptorSet, 0, GCheckerboardTexture.ImageView);
		vkUpdateDescriptorSets(GDevice.Device, WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, GCheckerboardTexture.GetWidth() / 8, GCheckerboardTexture.GetHeight() / 8, 1);

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GCheckerboardTexture.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	FBuffer StagingBuffer;
	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GHeightMap.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		auto FillHeightMap = [](void* Data, uint32 Width, uint32 Height)
		{
			float* Out = (float*)Data;
			while (Height--)
			{
				uint32 InnerWidth = Width;
				while (InnerWidth--)
				{
					*Out++ = (float)rand() / (float)RAND_MAX;
				}
			}
		};
		MapAndFillImageSync(StagingBuffer, CmdBuffer, &GHeightMap.Image, FillHeightMap);
		FlushMappedBuffer(GDevice.Device, &StagingBuffer);

		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GHeightMap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();

	StagingBuffer.Destroy(GDevice.Device);
}

bool DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height)
{
	LPSTR CmdLine = ::GetCommandLineA();
	const char* Token = CmdLine;
	while (Token = strchr(Token, ' '))
	{
		++Token;
		if (!_strcmpi(Token, "-debugger"))
		{
			while (!::IsDebuggerPresent())
			{
				Sleep(0);
			}
		}
	}

	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice(GDevice);

	GSwapchain.Create(GInstance.Surface, GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	GCmdBufferMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);

	GMemMgr.Create(GDevice.Device, GDevice.PhysicalDevice);

	GDescriptorPool.Create(GDevice.Device);

	GObjectCache.Create(&GDevice);

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
	ViewUB.Proj = CalculateProjectionMatrix(ToRadians(60), (float)GSwapchain.GetWidth() / (float)GSwapchain.GetHeight(), 0.1f, 1000.0f);

	FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
	ObjUB.Obj = FMatrix4x4::GetIdentity();

	CreateAndFillTexture();

	GSceneColorAfterPost.Create(GDevice.Device, GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	GSampler.Create(GDevice.Device);
	GSceneColor.Create(GDevice.Device, GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	GDepthBuffer.Create(GDevice.Device, GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);

	{
		// Setup on Present layout
		auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
		CmdBuffer->Begin();
		GSwapchain.ClearAndTransitionToPresent(CmdBuffer);
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GDepthBuffer.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
		CmdBuffer->End();
		GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
		CmdBuffer->WaitForFence();
	}

	return true;
}

static void RenderFrame(VkDevice Device, FCmdBuffer* CmdBuffer, uint32 Width, uint32 Height, VkImageView ColorImageView, VkFormat ColorFormat, FImage2DWithView* DepthBuffer)
{
	auto* RenderPass = GObjectCache.GetOrCreateRenderPass(Width, Height, 1, &ColorFormat, DepthBuffer->GetFormat());
	CmdBuffer->BeginRenderPass(RenderPass->RenderPass, *GObjectCache.GetOrCreateFramebuffer(RenderPass->RenderPass, ColorImageView, DepthBuffer->GetImageView(), Width, Height));

	auto* GfxPipeline = GObjectCache.GetOrCreateGfxPipeline(&GTestPSO, Width, Height, RenderPass->RenderPass);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->Pipeline);

	FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
	static float AngleDegrees = 0;
	{
		AngleDegrees += 360.0f / 10.0f / 60.0f;
		AngleDegrees = fmod(AngleDegrees, 360.0f);
	}
	ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees));

	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
		WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GObjUB);
		WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, /*GCheckerboardTexture*/GHeightMap.ImageView);
		vkUpdateDescriptorSets(Device, WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);

		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}
	{
		VkViewport Viewport;
		MemZero(Viewport);
		Viewport.width = (float)Width;
		Viewport.height = (float)Height;
		Viewport.maxDepth = 1;
		vkCmdSetViewport(CmdBuffer->CmdBuffer, 0, 1, &Viewport);

		VkRect2D Scissor;
		MemZero(Scissor);
		Scissor.extent.width = Width;
		Scissor.extent.height = Height;
		vkCmdSetScissor(CmdBuffer->CmdBuffer, 0, 1, &Scissor);
	}

	{
		VkDeviceSize Offset = 0;
		vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GObjVB.Buffer, &Offset);
		vkCmdDraw(CmdBuffer->CmdBuffer, GObj.Faces.size() * 3, 1, 0, 0);
	}

	CmdBuffer->EndRenderPass();
}

void RenderPost(VkDevice Device, FCmdBuffer* CmdBuffer, FImage2DWithView* SceneColor, FImage2DWithView* SceneColorAfterPost)
{
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, SceneColor->GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, SceneColorAfterPost->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	auto* ComputePipeline = GObjectCache.GetOrCreateComputePipeline(&GTestComputePostPSO);

	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->Pipeline);

	{
		auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestComputePSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddStorageImage(DescriptorSet, 0, SceneColor->ImageView);
		WriteDescriptors.AddStorageImage(DescriptorSet, 1, SceneColorAfterPost->ImageView);
		vkUpdateDescriptorSets(GDevice.Device, WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, SceneColorAfterPost->Image.Width / 8, SceneColorAfterPost->Image.Height / 8, 1);
}

void DoRender()
{
	if (GQuitting)
	{
		return;
	}
	auto* CmdBuffer = GCmdBufferMgr.GetActiveCmdBuffer();
	CmdBuffer->Begin();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GSceneColor.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	//TestCompute(CmdBuffer);

	VkFormat ColorFormat = (VkFormat)GSwapchain.BACKBUFFER_VIEW_FORMAT;
	RenderFrame(GDevice.Device, CmdBuffer, GSceneColor.GetWidth(), GSceneColor.GetHeight(), GSceneColor.GetImageView(), GSceneColor.GetFormat(), &GDepthBuffer);
	RenderPost(GDevice.Device, CmdBuffer, &GSceneColor, &GSceneColorAfterPost);

	// Blit post into scene color
	GSwapchain.AcquireNextImage();
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		uint32 Width = min(GSwapchain.GetWidth(), GSceneColorAfterPost.GetWidth());
		uint32 Height = min(GSwapchain.GetHeight(), GSceneColorAfterPost.GetHeight());
		BlitColorImage(CmdBuffer, Width, Height, GSceneColorAfterPost.GetImage(), VK_IMAGE_LAYOUT_GENERAL, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_UNDEFINED);
	}
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer->End();

	// First submit needs to wait for present semaphore
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, &GSwapchain.PresentCompleteSemaphores[GSwapchain.PresentCompleteSemaphoreIndex], &GSwapchain.RenderingSemaphores[GSwapchain.AcquiredImageIndex]);

	GSwapchain.Present(GDevice.PresentQueue);
}

void DoResize(uint32 Width, uint32 Height)
{
	if (Width != GSwapchain.GetWidth() && Height != GSwapchain.GetHeight())
	{
		vkDeviceWaitIdle(GDevice.Device);
		GSwapchain.Destroy();
		GObjectCache.Destroy();
		GSwapchain.Create(GInstance.Surface, GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);
		GObjectCache.Create(&GDevice);
	}
}

void DoDeinit()
{
	GQuitting = true;

	checkVk(vkDeviceWaitIdle(GDevice.Device));

	GObjectCache.Destroy();
	GCmdBufferMgr.Destroy();

	GTriVB.Destroy(GDevice.Device);
	GViewUB.Destroy(GDevice.Device);
	GObjUB.Destroy(GDevice.Device);
	GObjVB.Destroy(GDevice.Device);

	GSceneColorAfterPost.Destroy();
	GSampler.Destroy();

	GDepthBuffer.Destroy();
	GSceneColor.Destroy();
	GCheckerboardTexture.Destroy();
	GHeightMap.Destroy();

	GDescriptorPool.Destroy();

	GTestComputePostPSO.Destroy(GDevice.Device);
	GTestComputePSO.Destroy(GDevice.Device);
	GTestPSO.Destroy(GDevice.Device);
	GFillTexturePSO.Destroy(GDevice.Device);

	GSwapchain.Destroy();

#if 0
	GResourceRecycler.Deinit();
#endif
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
