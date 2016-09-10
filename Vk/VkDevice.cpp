// Implementations

#include "stdafx.h"
#include "VkDevice.h"
#include "VkResources.h"

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

VkBool32 FInstance::DebugReportCallback(VkDebugReportFlagsEXT Flags, VkDebugReportObjectTypeEXT ObjectType, uint64_t Object, size_t Location, int32_t MessageCode, const char* LayerPrefix, const char* Message, void* UserData)
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
	else if (Flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		char s[2048];
		sprintf_s(s, "<VK>Perf: %s\n", Message);
		::OutputDebugStringA(s);
		++n;
	}
	else if (1)
	{
		static const char* SkipPrefixes[] =
		{
			"ObjectTracker",
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

void FSwapchain::Create(VkSurfaceKHR SurfaceKHR, VkPhysicalDevice PhysicalDevice, VkDevice InDevice, VkSurfaceKHR Surface, uint32& WindowWidth, uint32& WindowHeight)
{
	Device = InDevice;

	uint32 NumFormats = 0;
	checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, SurfaceKHR, &NumFormats, nullptr));
	std::vector<VkSurfaceFormatKHR> Formats;
	Formats.resize(NumFormats);
	checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, SurfaceKHR, &NumFormats, &Formats[0]));

	VkFormat ColorFormat;
	check(NumFormats > 0);
	if (NumFormats == 1 && Formats[0].format == VK_FORMAT_UNDEFINED)
	{
		ColorFormat = (VkFormat)SWAPCHAIN_IMAGE_FORMAT;
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
			break;
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
	CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
		ImageViews[Index].Create(Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, (VkFormat)BACKBUFFER_VIEW_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);
		PresentCompleteSemaphores[Index].Create(Device);
		RenderingSemaphores[Index].Create(Device);
	}
}

void FRenderPass::Create(VkDevice InDevice, const FRenderPassLayout& InLayout)
{
	Device = InDevice;
	Layout = InLayout;

	VkAttachmentDescription AttachmentDesc[1 + FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
	VkAttachmentReference AttachmentRef[1 + FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
	MemZero(AttachmentDesc);
	MemZero(AttachmentRef);

	VkAttachmentDescription* CurrentDesc = AttachmentDesc;
	VkAttachmentReference* CurrentRef = AttachmentRef;
	uint32 Index = 0;
	for (Index = 0; Index < Layout.NumColorTargets; ++Index)
	{
		CurrentDesc->format = Layout.ColorFormats[Index];
		CurrentDesc->samples = VK_SAMPLE_COUNT_1_BIT;
		CurrentDesc->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		CurrentDesc->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		CurrentDesc->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrentDesc->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrentDesc->initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CurrentDesc->finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		CurrentRef->attachment = Index;
		CurrentRef->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		++CurrentDesc;
		++CurrentRef;
	}

	VkAttachmentReference* DepthRef = nullptr;
	if (Layout.DepthStencilFormat != VK_FORMAT_UNDEFINED)
	{
		CurrentDesc->format = Layout.DepthStencilFormat;
		CurrentDesc->samples = VK_SAMPLE_COUNT_1_BIT;
		CurrentDesc->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		CurrentDesc->storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrentDesc->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrentDesc->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrentDesc->initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		CurrentDesc->finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		++CurrentDesc;

		DepthRef = CurrentRef;
		CurrentRef->attachment = Index;
		CurrentRef->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		++CurrentRef;
	}


	VkSubpassDescription Subpass;
	MemZero(Subpass);
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = Index;
	Subpass.pColorAttachments = &AttachmentRef[0];
	Subpass.pDepthStencilAttachment = DepthRef;

	VkRenderPassCreateInfo RenderPassInfo;
	MemZero(RenderPassInfo);
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	RenderPassInfo.attachmentCount = CurrentDesc - AttachmentDesc;
	RenderPassInfo.pAttachments = AttachmentDesc;
	RenderPassInfo.subpassCount = 1;
	RenderPassInfo.pSubpasses = &Subpass;

	checkVk(vkCreateRenderPass(Device, &RenderPassInfo, nullptr, &RenderPass));
}

FGfxPipeline::FGfxPipeline()
{
	MemZero(IAInfo);
	IAInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	IAInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	//IAInfo.primitiveRestartEnable = VK_FALSE;

	MemZero(Viewport);
	//Viewport.x = 0;
	//Viewport.y = 0;
	//Viewport.width = (float)Width;
	//Viewport.height = (float)Height;
	//Viewport.minDepth = 0;
	Viewport.maxDepth = 1;

	MemZero(Scissor);
	//scissors.offset = { 0, 0 };
	//Scissor.extent = { Width, Height };

	MemZero(ViewportInfo);
	ViewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	ViewportInfo.viewportCount = 1;
	ViewportInfo.pViewports = &Viewport;
	ViewportInfo.scissorCount = 1;
	ViewportInfo.pScissors = &Scissor;

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

	MemZero(MSInfo);
	MSInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	MSInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	//MSInfo.sampleShadingEnable = VK_FALSE;
	//MSInfo.minSampleShading = 0;
	//MSInfo.pSampleMask = NULL;
	//MSInfo.alphaToCoverageEnable = VK_FALSE;
	//MSInfo.alphaToOneEnable = VK_FALSE;

	MemZero(Stencil);
	Stencil.failOp = VK_STENCIL_OP_KEEP;
	Stencil.passOp = VK_STENCIL_OP_KEEP;
	Stencil.depthFailOp = VK_STENCIL_OP_KEEP;
	Stencil.compareOp = VK_COMPARE_OP_ALWAYS;
	//Stencil.compareMask = 0;
	//Stencil.writeMask = 0;
	//Stencil.reference = 0;

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

	MemZero(AttachState);
	//AtachState.blendEnable = VK_FALSE;
	AttachState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
	AttachState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	AttachState.colorBlendOp = VK_BLEND_OP_ADD;
	AttachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	AttachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	AttachState.alphaBlendOp = VK_BLEND_OP_ADD;
	AttachState.colorWriteMask = 0xf;

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

	Dynamic[0] = VK_DYNAMIC_STATE_VIEWPORT;
	Dynamic[1] = VK_DYNAMIC_STATE_SCISSOR;

	MemZero(DynamicInfo);
	DynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	DynamicInfo.dynamicStateCount = 2;
	DynamicInfo.pDynamicStates = Dynamic;
}

void FGfxPipeline::Create(VkDevice Device, FGfxPSO* PSO, FVertexFormat* VertexFormat, uint32 Width, uint32 Height, VkRenderPass RenderPass)
{
	std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
	PSO->SetupShaderStages(ShaderStages);

	VkPipelineLayoutCreateInfo CreateInfo;
	MemZero(CreateInfo);
	CreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	CreateInfo.setLayoutCount = 1;
	CreateInfo.pSetLayouts = &PSO->DSLayout;
	checkVk(vkCreatePipelineLayout(Device, &CreateInfo, nullptr, &PipelineLayout));

	VkPipelineVertexInputStateCreateInfo VIInfo = VertexFormat->GetCreateInfo();

	//Viewport.x = 0;
	//Viewport.y = 0;
	Viewport.width = (float)Width;
	Viewport.height = (float)Height;
	//Viewport.minDepth = 0;
	//Viewport.maxDepth = 1;

	//scissors.offset = { 0, 0 };
	Scissor.extent = { Width, Height };

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
