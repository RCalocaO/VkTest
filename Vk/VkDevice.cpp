// Implementations

#include "stdafx.h"
#include "VkDevice.h"
#include "VkResources.h"
#include <set>

static bool GSkipValidation = false;

void FInstance::GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
{
	if (!GSkipValidation)
	{
		uint32 NumLayers;
		checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, nullptr));
		if (NumLayers > 0)
		{
			std::vector<VkLayerProperties> InstanceProperties;
			InstanceProperties.resize(NumLayers);

			checkVk(vkEnumerateInstanceLayerProperties(&NumLayers, &InstanceProperties[0]));

			for (auto& Property : InstanceProperties)
			{
				std::string s = "Found Layer: ";
				s += Property.layerName;
				s += "\n";
				::OutputDebugStringA(s.c_str());
			}

			const char* UseValidationLayers[] =
			{
				//"VK_LAYER_LUNARG_api_dump",
				"VK_LAYER_LUNARG_core_validation",
				"VK_LAYER_LUNARG_standard_validation",
				"VK_LAYER_LUNARG_image",
				"VK_LAYER_LUNARG_object_tracker",
				"VK_LAYER_LUNARG_parameter_validation",
				"VK_LAYER_LUNARG_screenshot",
				"VK_LAYER_LUNARG_swapchain",
				"VK_LAYER_GOOGLE_threading",
				"VK_LAYER_GOOGLE_unique_objects",
				//"VK_LAYER_RENDERDOC_Capture",
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
		sprintf_s(s, "<VK>Error[%s:%d] %s\n", LayerPrefix, MessageCode, Message);
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

		uint32 Size = (uint32)strlen(Message) + 100;
		auto* s = new char[Size];
		snprintf(s, Size - 1, "<VK>: %s\n", Message);
		::OutputDebugStringA(s);
		delete[] s;
	}

	return false;
}

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

void FSwapchain::Create(VkSurfaceKHR SurfaceKHR, VkPhysicalDevice PhysicalDevice, VkDevice InDevice, VkSurfaceKHR Surface, uint32& WindowWidth, uint32& WindowHeight)
{
	Device = InDevice;

	uint32 NumFormats = 0;
	checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, SurfaceKHR, &NumFormats, nullptr));
	std::vector<VkSurfaceFormatKHR> Formats;
	Formats.resize(NumFormats);
	checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, SurfaceKHR, &NumFormats, &Formats[0]));

	check(NumFormats > 0);
	Format = Formats[0].format;

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
	CreateInfo.imageFormat = Format;
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
		ImageViews[Index].Create(Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, Format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		PresentCompleteSemaphores[Index].Create(Device);
		RenderingSemaphores[Index].Create(Device);
	}
}

void FRenderPass::Create(VkDevice InDevice, const FRenderPassLayout& InLayout)
{
	Device = InDevice;
	Layout = InLayout;

	std::vector<VkAttachmentDescription> Descriptions;
	std::vector<VkAttachmentReference> AttachmentReferences;
	std::vector<VkAttachmentReference> ResolveReferences;
	int32 DepthReferenceIndex = -1;
	/*
	VkAttachmentDescription AttachmentDesc[1 + FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
	VkAttachmentReference AttachmentRef[1 + FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
	VkAttachmentReference ResolveRef[1 + FRenderPassLayout::MAX_COLOR_ATTACHMENTS];
	MemZero(AttachmentDesc);
	MemZero(AttachmentRef);
	MemZero(ResolveRef);

	VkAttachmentDescription* CurrentDesc = AttachmentDesc;
	VkAttachmentReference* CurrentRef = AttachmentRef;
	uint32 Index = 0;

	VkAttachmentReference* DepthRef = nullptr;
	if (Layout.DepthStencilFormat != VK_FORMAT_UNDEFINED)
	{
		CurrentDesc->format = Layout.DepthStencilFormat;
		CurrentDesc->samples = Layout.NumSamples;
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
		++Index;
	}
*/
	for (uint32 Index = 0; Index < Layout.NumColorTargets; ++Index)
	{
		VkAttachmentDescription Desc;
		MemZero(Desc);
		Desc.format = Layout.ColorFormats[Index];
		Desc.samples = Layout.NumSamples;
		Desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Desc.storeOp = (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
		Desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Descriptions.push_back(Desc);

		VkAttachmentReference Reference;
		MemZero(Reference);
		Reference.attachment = (uint32)Descriptions.size() - 1;
		Reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		AttachmentReferences.push_back(Reference);
	}

	if (Layout.DepthStencilFormat != VK_FORMAT_UNDEFINED)
	{
		VkAttachmentDescription Desc;
		MemZero(Desc);
		Desc.format = Layout.DepthStencilFormat;
		Desc.samples = Layout.NumSamples;
		Desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Desc.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Descriptions.push_back(Desc);

		DepthReferenceIndex = (int32)AttachmentReferences.size();
		VkAttachmentReference Reference;
		MemZero(Reference);
		Reference.attachment = (uint32)Descriptions.size() - 1;
		Reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		AttachmentReferences.push_back(Reference);
	}

	if (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED)
	{
		VkAttachmentDescription Desc;
		MemZero(Desc);
		Desc.format = Layout.ColorFormats[0];
		Desc.samples = VK_SAMPLE_COUNT_1_BIT;
		Desc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		Desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Desc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Desc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Descriptions.push_back(Desc);

		VkAttachmentReference Reference;
		MemZero(Reference);
		Reference.attachment = (uint32)Descriptions.size() - 1;
		Reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		ResolveReferences.push_back(Reference);
	}

/*
	if (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED)
	{
		CurrentDesc->format = Layout.ResolveFormat;
		CurrentDesc->samples = VK_SAMPLE_COUNT_1_BIT;
		CurrentDesc->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		CurrentDesc->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		CurrentDesc->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		CurrentDesc->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		CurrentDesc->initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		CurrentDesc->finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		ResolveRef.attachment = Index;
		ResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		++CurrentDesc;
		++Index;
	}
*/
	VkSubpassDescription Subpass;
	MemZero(Subpass);
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = Layout.NumColorTargets;
	Subpass.pColorAttachments = &AttachmentReferences[0];
	Subpass.pResolveAttachments = (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED) ? &ResolveReferences[0] : nullptr;
	Subpass.pDepthStencilAttachment = DepthReferenceIndex != -1 ? &AttachmentReferences[DepthReferenceIndex] : nullptr;

	if (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED)
	{
		//Subpass.pResolveAttachments = &ResolveRef;
	}

	VkRenderPassCreateInfo RenderPassInfo;
	MemZero(RenderPassInfo);
	RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	RenderPassInfo.attachmentCount = (uint32)Descriptions.size();
	RenderPassInfo.pAttachments = &Descriptions[0];
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
	RSInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	RSInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
	Stencil.compareMask = 0xff;
	Stencil.writeMask = 0xff;
	//Stencil.reference = 0;

	MemZero(DSInfo);
	DSInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	DSInfo.depthTestEnable = VK_TRUE;
	DSInfo.depthWriteEnable = VK_TRUE;
	DSInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	//DSInfo.depthBoundsTestEnable = VK_FALSE;
	//DSInfo.stencilTestEnable = VK_FALSE;
	DSInfo.front = Stencil;
	//DSInfo.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
	DSInfo.back = Stencil;
	//DSInfo.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
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

void FGfxPipeline::Create(VkDevice Device, const FGfxPSO* PSO, const FVertexFormat* VertexFormat, uint32 Width, uint32 Height, const FRenderPass* RenderPass)
{
	std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
	PSO->SetupShaderStages(ShaderStages);

	VkPipelineLayoutCreateInfo CreateInfo;
	MemZero(CreateInfo);
	CreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	CreateInfo.setLayoutCount = 1;
	CreateInfo.pSetLayouts = &PSO->DSLayout;
	checkVk(vkCreatePipelineLayout(Device, &CreateInfo, nullptr, &PipelineLayout));

	VkPipelineVertexInputStateCreateInfo VIInfo;
	if (VertexFormat)
	{
		VIInfo = VertexFormat->GetCreateInfo();
	}
	else
	{
		MemZero(VIInfo);
		VIInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	}

	//Viewport.x = 0;
	//Viewport.y = 0;
	Viewport.width = (float)Width;
	Viewport.height = (float)Height;
	//Viewport.minDepth = 0;
	//Viewport.maxDepth = 1;

	MSInfo.rasterizationSamples = RenderPass->GetLayout().GetNumSamples();

	//scissors.offset = { 0, 0 };
	Scissor.extent = { Width, Height };

	VkGraphicsPipelineCreateInfo PipelineInfo;
	MemZero(PipelineInfo);
	PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	PipelineInfo.stageCount = (uint32)ShaderStages.size();
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
	PipelineInfo.renderPass = RenderPass->RenderPass;
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

		for (uint32 Index = (uint32)FreeList.size() - 1; Index > 0; --Index)
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

void FCmdBuffer::BeginRenderPass(VkRenderPass RenderPass, const FFramebuffer& Framebuffer, bool bHasSecondary)
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
	vkCmdBeginRenderPass(CmdBuffer, &BeginInfo, bHasSecondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);

	State = EState::InsideRenderPass;
}

#include <vulkan/spirv.h>

void FShader::GenerateReflection(std::map<uint32, FDescriptorSetInfo>& DescriptorSets)
{
	uint32* StartWord = (uint32*)&SpirV[0];
	uint32* Word = StartWord;
	check(*Word++ == SpvMagicNumber);	// Magic
	*Word++; // Version
	*Word++; // Generator
	uint32 Bound = *Word++;
	*Word++; // Schema

	std::map<uint32, std::string> NameMap;
	std::set<uint32> BlockMap;
	std::map<uint32, uint32> DescriptorSetMap;
	std::map<uint32, uint32> BindingMap;
	std::map<uint32, uint32> SampledImageMap;
	std::map<uint32, uint32> ImageMap;
	std::set<uint32> StorageImageMap;
	std::map<uint32, uint32> PointerMap;

	auto LiteralString = [](uint32*& Word, uint32 WordCount)
	{
		std::string Name;
		for (uint32 Index = 0; Index < WordCount; ++Index)
		{
			uint32 Name4 = *Word++;
			for (int32 SubIndex = 0; SubIndex < 4; ++SubIndex)
			{
				char c = Name4 & 0x7f;
				if (!c)
				{
					break;
				}
				Name += c;
				Name4 >>= 8;
			}
		}
		return Name;
	};

	SpvOp PrevOpCode;
	bool bInFunction = false;
	while (Word < StartWord + SpirV.size() / 4)
	{
		SpvOp OpCode = (SpvOp)(*Word & SpvOpCodeMask);
		uint32 WordCount = (*Word >> SpvWordCountShift);
		++Word;
		check(WordCount >= 1);
		switch (OpCode)
		{
		case SpvOpName:
		{
			uint32 Id = *Word++;
			NameMap[Id] = LiteralString(Word, WordCount - 2);
		}
			break;
		case SpvOpFunction:
		{
			uint32 ResultType = *Word++;
			uint32 ResultId = *Word++;
			SpvFunctionControlMask FunctionControl = (SpvFunctionControlMask)*Word++;
			uint32 FunctionType = *Word++;
			check(!bInFunction);
			bInFunction = true;
		}
			break;
		case SpvOpFunctionEnd:
		{
			// 1 WordCount
			check(bInFunction);
			bInFunction = false;
		}
			break;
#if 0
		case SpvOpMemberName:
		{
			uint32 StructId = *Word++;
			uint32 MemberIndex = *Word++;
			check(MemberIndex == StructNameMap[StructId].Members.size());
			StructNameMap[StructId].Members.push_back(LiteralString(Word, WordCount - 3));
		}
			break;
#endif
		case SpvOpTypeImage:
		{
			uint32 ResultId = *Word++;
			uint32 SampledType = *Word++;
			SpvDim Dim = (SpvDim)*Word++;
			uint32 Depth = *Word++;	// 0 means not depth, 1 means depth, 2 means no indication
			uint32 Arrayed = *Word++;	// 1 means array
			uint32 MS = *Word++; // 0 means single sample, 1 means multisample
			uint32 Sampled = *Word++;
			switch (Sampled)
			{
			case 0:	// indicates this is only known at run time, not at compile time
				break;
			case 1: // indicates will be used with sampler
				break;
			case 2: // indicates will be used without a sampler(a storage image)
				StorageImageMap.insert(ResultId);
				break;
			default:
				check(0);
				break;
			}
			SpvImageFormat Format = (SpvImageFormat)*Word++;
			if (WordCount > 9)
			{
				check(WordCount == 10)
				SpvAccessQualifier Access = (SpvAccessQualifier)*Word++;
			}
			else
			{
				check(WordCount == 9)
			}
			ImageMap[ResultId] = SampledType;
		}
			break;
		case SpvOpTypeSampledImage:
		{
			uint32 ResultId = *Word++;
			uint32 ImageType = *Word++;
			SampledImageMap[ResultId] = ImageType;
		}
			break;
		case SpvOpTypePointer:
		{
			uint32 ResultId = *Word++;
			uint32 StorageClass = *Word++;
			uint32 Type = *Word++;
			PointerMap[ResultId] = Type;
		}
			break;
		case SpvOpVariable:
		{
			uint32 ResultType = *Word++;
			uint32 ResultId = *Word++;
			SpvStorageClass StorageClass = (SpvStorageClass)*Word++;
			//if (StorageClass == SpvStorageClassUniform)
			//{
			//	UniformVariableMap[ResultId] = ResultType;
			//}
			for (uint32 Index = 4; Index < WordCount; ++Index)
			{
				uint32 Initializer = *Word++;
				Initializer = Initializer;
			}

			// Globals
			if (!bInFunction)
			{
				switch (StorageClass)
				{
				case SpvStorageClassUniform:
				{
					auto PointerFound = PointerMap.find(ResultType);
					check(PointerFound != PointerMap.end());
					auto NameFound = NameMap.find(PointerFound->second);
					check(NameFound != NameMap.end());
					auto BindingFound = BindingMap.find(ResultId);
					check(BindingFound != BindingMap.end());
					auto DescriptorSetFound = DescriptorSetMap.find(ResultId);
					check(DescriptorSetFound != DescriptorSetMap.end());
					auto BlockFound = BlockMap.find(PointerFound->second);
					FDescriptorSetInfo& Info = DescriptorSets[DescriptorSetFound->second];
					Info.DescriptorSetIndex = DescriptorSetFound->second;
					Info.Bindings[BindingFound->second].Name = NameFound->second;
					Info.Bindings[BindingFound->second].BindingIndex = BindingFound->second;
					if (BlockFound != BlockMap.end())
					{
						Info.Bindings[BindingFound->second].Type = FDescriptorSetInfo::FBindingInfo::EType::UniformBuffer;
					}
					else
					{
						Info.Bindings[BindingFound->second].Type = FDescriptorSetInfo::FBindingInfo::EType::StorageBuffer;
					}
				}
					break;
				case SpvStorageClassUniformConstant:
				{
					auto NameFound = NameMap.find(ResultId);
					check(NameFound != NameMap.end());
					auto BindingFound = BindingMap.find(ResultId);
					check(BindingFound != BindingMap.end());
					auto DescriptorSetFound = DescriptorSetMap.find(ResultId);
					check(DescriptorSetFound != DescriptorSetMap.end());
					auto PointerFound = PointerMap.find(ResultType);
					if (PointerFound != PointerMap.end())
					{
						uint32 PointerType = PointerFound->second;
						if (SampledImageMap.find(PointerType) != SampledImageMap.end())
						{
							FDescriptorSetInfo& Info = DescriptorSets[DescriptorSetFound->second];
							Info.DescriptorSetIndex = DescriptorSetFound->second;
							Info.Bindings[BindingFound->second].Name = NameFound->second;
							Info.Bindings[BindingFound->second].BindingIndex = BindingFound->second;
							Info.Bindings[BindingFound->second].Type = FDescriptorSetInfo::FBindingInfo::EType::SampledImage;
						}
						else if (ImageMap.find(PointerType) != ImageMap.end())
						{
							FDescriptorSetInfo& Info = DescriptorSets[DescriptorSetFound->second];
							Info.DescriptorSetIndex = DescriptorSetFound->second;
							Info.Bindings[BindingFound->second].Name = NameFound->second;
							Info.Bindings[BindingFound->second].BindingIndex = BindingFound->second;
							if (StorageImageMap.find(PointerType) != StorageImageMap.end())
							{
								Info.Bindings[BindingFound->second].Type = FDescriptorSetInfo::FBindingInfo::EType::StorageImage;
							}
							else
							{
								Info.Bindings[BindingFound->second].Type = FDescriptorSetInfo::FBindingInfo::EType::Image;
							}
						}
						else
						{
							check(0);
						}
					}
					else
					{
						check(0);
					}
				}
					break;
				case SpvStorageClassInput:
				case SpvStorageClassOutput:
					// Continue for now
					break;
				default:
					check(0);
					break;
				}
			}
		}
			break;
		case SpvOpDecorate:
		{
			uint32 Id = *Word++;
			uint32 Decoration = *Word++;
			switch (Decoration)
			{
			case SpvDecorationBlock:
				BlockMap.insert(Id);
				break;
			case SpvDecorationDescriptorSet:
				DescriptorSetMap[Id] = *Word++;
				check(WordCount == 4);
				break;
			case SpvDecorationBinding:
				BindingMap[Id] = *Word++;
				check(WordCount == 4);
				break;
			default:
				Word += WordCount - 3;
				break;
			}
		}
			break;
		default:
			Word += WordCount - 1;
			break;
		}

		PrevOpCode = OpCode;
	}
}


void FPSO::CompareAgainstReflection(std::vector<VkDescriptorSetLayoutBinding>& Bindings)
{
	auto DSInfoCopy = DescriptorSetInfo;

	//#todo: Fix to work with more than one Descriptor Set
	check(DSInfoCopy.size() <= 1);
	for (auto& Binding : Bindings)
	{
		auto FoundBinding = DSInfoCopy[0].Bindings.find(Binding.binding);
		if (FoundBinding != DSInfoCopy[0].Bindings.end())
		{
			auto& FoundBindingInfo = FoundBinding->second;
			switch (Binding.descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::UniformBuffer);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::StorageBuffer);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::SampledImage);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::StorageImage);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			default:
				check(0);
				break;
			}
		}
		else
		{
			// Extra binding set from C++
			//check(0);
		}
	}

	check(DSInfoCopy[0].Bindings.empty());
}

void FDescriptorPool::RefreshFences()
{
	for (auto& Pair : Sets)
	{
		auto& Used = Pair.second.Used;
		for (int32 Index = (int32)Used.size() - 1; Index >= 0; --Index)
		{
			if (Used[Index]->FenceCounter < Used[Index]->UsedFence->FenceSignaledCounter)
			{
				FDescriptorSet* Set = Used[Index];
				Used[Index] = Used.back();
				Used.pop_back();
				Pair.second.Free.push_back(Set);
			}
		}
	}
}
	
void FDescriptorPool::UpdateDescriptors(FWriteDescriptors& InWriteDescriptors)
{
	check(!InWriteDescriptors.bClosed);
	if (!InWriteDescriptors.DSWrites.empty())
	{
		vkUpdateDescriptorSets(Device, (uint32)InWriteDescriptors.DSWrites.size(), &InWriteDescriptors.DSWrites[0], 0, nullptr);
	}
	InWriteDescriptors.bClosed = true;
}


void FCmdBufferMgr::Submit(FDescriptorPool& DescriptorPool, FPrimaryCmdBuffer* CmdBuffer, VkQueue Queue, FSemaphore* WaitSemaphore, FSemaphore* SignaledSemaphore)
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
	DescriptorPool.RefreshFences();
}
