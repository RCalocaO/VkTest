// Implementations

#include "stdafx.h"
#include "VkDevice.h"
#include "VkResources.h"
#include <vulkan/spirv.hpp>
#include <set>
#include "../../SPIRV-Cross/spirv_cross.hpp"

bool GValidation = false;
bool GRenderDoc = false;
bool GVkTrace = false;

void FInstance::GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
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
			std::string s = "Found Instance Layer: ";
			s += Property.layerName;
			s += "\n";
			::OutputDebugStringA(s.c_str());
		}

		if (GValidation)
		{
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

		if (GRenderDoc)
		{
			for (auto& Prop : InstanceProperties)
			{
				if (!strcmp(Prop.layerName, "VK_LAYER_RENDERDOC_Capture"))
				{
					OutLayers.push_back("VK_LAYER_RENDERDOC_Capture");
					break;
				}
			}
		}

		if (GVkTrace)
		{
			for (auto& Prop : InstanceProperties)
			{
				if (!strcmp(Prop.layerName, "VK_LAYER_LUNARG_vktrace"))
				{
					OutLayers.push_back("VK_LAYER_LUNARG_vktrace");
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
			std::string s = "Found Instance Extension: ";
			s += Extension.extensionName;
			s += "\n";
			::OutputDebugStringA(s.c_str());
		}

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

		OutDevice.PhysicalDevice = Devices[Index];
		OutDevice.DeviceProperties = DeviceProperties;

		for (uint32 QueueIndex = 0; QueueIndex < NumQueueFamilies; ++QueueIndex)
		{
			VkBool32 bSupportsPresent;
			checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(Devices[Index], QueueIndex, Surface, &bSupportsPresent));
			if (bSupportsPresent && (QueueFamilies[QueueIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT && OutDevice.PresentQueueFamilyIndex == UINT32_MAX)
			{
				OutDevice.PresentQueueFamilyIndex = QueueIndex;
			}
			else if ((QueueFamilies[QueueIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) == VK_QUEUE_COMPUTE_BIT && OutDevice.ComputeQueueFamilyIndex == UINT32_MAX && OutDevice.PresentQueueFamilyIndex != UINT32_MAX)
			{
				OutDevice.ComputeQueueFamilyIndex = QueueIndex;
			}
			else if ((QueueFamilies[QueueIndex].queueFlags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT && OutDevice.TransferQueueFamilyIndex == UINT32_MAX && OutDevice.PresentQueueFamilyIndex != UINT32_MAX)
			{
				OutDevice.TransferQueueFamilyIndex = QueueIndex;
			}
		}
	}

	if (OutDevice.PresentQueueFamilyIndex == UINT32_MAX)
	{
		// Not found!
		check(0);
		return;
	}

	if (OutDevice.TransferQueueFamilyIndex == UINT32_MAX)
	{
		OutDevice.TransferQueueFamilyIndex = OutDevice.PresentQueueFamilyIndex;
	}

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
		ImageViews[Index].Create(Device, Images[Index], VK_IMAGE_VIEW_TYPE_2D, Format, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
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
		Desc.storeOp = (InLayout.ResolveColorFormat != VK_FORMAT_UNDEFINED) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
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

	if (InLayout.ResolveColorFormat != VK_FORMAT_UNDEFINED)
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

	if (InLayout.ResolveDepthFormat != VK_FORMAT_UNDEFINED)
	{
		VkAttachmentDescription Desc;
		MemZero(Desc);
		Desc.format = Layout.DepthStencilFormat;
		Desc.samples = VK_SAMPLE_COUNT_1_BIT;
		Desc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		Desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Desc.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Desc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		Descriptions.push_back(Desc);

		VkAttachmentReference Reference;
		MemZero(Reference);
		Reference.attachment = (uint32)Descriptions.size() - 1;
		Reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		ResolveReferences.push_back(Reference);
	}

	VkSubpassDescription Subpass;
	MemZero(Subpass);
	Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	Subpass.colorAttachmentCount = Layout.NumColorTargets;
	Subpass.pColorAttachments = &AttachmentReferences[0];
	Subpass.pResolveAttachments = (InLayout.ResolveColorFormat != VK_FORMAT_UNDEFINED || InLayout.ResolveDepthFormat != VK_FORMAT_UNDEFINED) ? &ResolveReferences[0] : nullptr;
	Subpass.pDepthStencilAttachment = DepthReferenceIndex != -1 ? &AttachmentReferences[DepthReferenceIndex] : nullptr;

	//if (InLayout.ResolveFormat != VK_FORMAT_UNDEFINED)
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
	RSInfo.cullMode = VK_CULL_MODE_NONE;//VK_CULL_MODE_BACK_BIT;
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

void FGfxPipeline::Create(VkDevice Device, const FGfxPSO* InPSO, const FVertexFormat* VertexFormat, uint32 Width, uint32 Height, const FRenderPass* RenderPass)
{
	PSO = InPSO;
	PSO->Pipelines.push_back(this);

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

FMemPage::FMemPage(VkDevice InDevice, VkDeviceSize Size, uint32 InMemTypeIndex, VkMemoryPropertyFlags InMemPropertyFlags, bool bInMapped)
	: Allocation(InDevice, Size, MemTypeIndex, InMemPropertyFlags, bInMapped)
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

FMemSubAlloc* FMemPage::TryAlloc(uint64 Size, uint64 Alignment, const char* InFile, int InLine)
{
	for (size_t Index = 0; Index < FreeList.size(); ++Index)
	{
		auto& Range = FreeList[Index];
		uint64 AlignedOffset = Align(Range.Begin, Alignment);
		if (AlignedOffset + Size <= Range.End)
		{
			auto* SubAlloc = new FMemSubAlloc(Range.Begin, AlignedOffset, Size, this, InFile, InLine);
			SubAllocations.push_back(SubAlloc);
			Range.Begin = AlignedOffset + Size;
			if (Range.End == Range.Begin)
			{
				for (size_t Rest = Index + 1; Rest < FreeList.size(); ++Rest)
				{
					FreeList[Index] = FreeList[Rest];
				}
				FreeList.pop_back();
			}
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

	VkClearValue ClearValues[2] = { { 0.0f, 0.0f, 0.5f, 1.0f },{ 1.0, 0.0 } };
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

void FShader::GenerateReflection(std::map<uint32, FDescriptorSetInfo>& DescriptorSets)
{
	spirv_cross::Compiler Compiler((uint32*)&SpirV[0], SpirV.size() / 4);
	spirv_cross::ShaderResources Resources = Compiler.get_shader_resources();

	auto ParseResources = [&](std::vector<spirv_cross::Resource>& Resources, FDescriptorSetInfo::FBindingInfo::EType Type)
	{
		for (const spirv_cross::Resource& Resource : Resources)
		{
			uint32 Binding = Compiler.get_decoration(Resource.id, spv::DecorationBinding);
			uint32 Set = Compiler.get_decoration(Resource.id, spv::DecorationDescriptorSet);
			DescriptorSets[Set].DescriptorSetIndex = Set;
			DescriptorSets[Set].Bindings[Binding].BindingIndex = Binding;
			DescriptorSets[Set].Bindings[Binding].Name = Resource.name;
			DescriptorSets[Set].Bindings[Binding].Type = Type;
		}
	};

	ParseResources(Resources.uniform_buffers, FDescriptorSetInfo::FBindingInfo::EType::UniformBuffer);
	ParseResources(Resources.storage_buffers, FDescriptorSetInfo::FBindingInfo::EType::StorageBuffer);
	ParseResources(Resources.storage_images, FDescriptorSetInfo::FBindingInfo::EType::StorageImage);
	ParseResources(Resources.sampled_images, FDescriptorSetInfo::FBindingInfo::EType::CombinedSamplerImage);
	ParseResources(Resources.separate_images, FDescriptorSetInfo::FBindingInfo::EType::SampledImage);
	ParseResources(Resources.separate_samplers, FDescriptorSetInfo::FBindingInfo::EType::Sampler);
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
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::CombinedSamplerImage);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				check(FoundBindingInfo.Type == FDescriptorSetInfo::FBindingInfo::EType::Sampler);
				DSInfoCopy[0].Bindings.erase(FoundBindingInfo.BindingIndex);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
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


void FCmdBufferMgr::Submit(FPrimaryCmdBuffer* CmdBuffer, VkQueue Queue, std::vector<FSemaphore*>&& WaitSemaphores, FSemaphore* SignaledSemaphore)
{
	check(CmdBuffer->State == FPrimaryCmdBuffer::EState::Ended);
	check(CmdBuffer->Secondary.empty());
	VkPipelineStageFlags StageMask[2] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSubmitInfo Info;
	MemZero(Info);
	Info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	Info.pWaitDstStageMask = StageMask;
	Info.commandBufferCount = 1;
	Info.pCommandBuffers = &CmdBuffer->CmdBuffer;
	std::vector<VkSemaphore> WaitSemaphoresList;
	if (!WaitSemaphores.empty())
	{
		check(WaitSemaphores.size() <= 2);
		StageMask[0] = VK_PIPELINE_STAGE_TRANSFER_BIT;
		StageMask[1] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		Info.waitSemaphoreCount = (uint32)WaitSemaphores.size();
		std::for_each(WaitSemaphores.begin(), WaitSemaphores.end(), [&](const FSemaphore* S){ WaitSemaphoresList.push_back(S->Semaphore); });
		Info.pWaitSemaphores = &WaitSemaphoresList[0];
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

void FGfxPSO::Destroy(VkDevice Device)
{
	FPSO::Destroy(Device);
	Collection.DestroyShader(PS);
	Collection.DestroyShader(VS);
}

void FComputePSO::Destroy(VkDevice Device)
{
	FPSO::Destroy(Device);
	Collection.DestroyShader(CS);
}

void FComputePSO::SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages) const
{
	VkPipelineShaderStageCreateInfo Info;
	MemZero(Info);
	Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	Info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	Info.module = Collection.GetShaderModule(CS);
	Info.pName = Collection.GetEntryPoint(CS).c_str();
	OutShaderStages.push_back(Info);
}

bool FGfxPSO::CreateVSPS(VkDevice Device, FShaderHandle InVS, FShaderHandle InPS)
{
	VS = InVS;
	PS = InPS;
	((FShader*)(Collection.GetShader(VS)))->GenerateReflection(DescriptorSetInfo);
	((FShader*)(Collection.GetShader(PS)))->GenerateReflection(DescriptorSetInfo);
	CreateDescriptorSetLayout(Device);
	return true;
}

void FGfxPSO::SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages) const
{
	VkPipelineShaderStageCreateInfo Info;
	MemZero(Info);
	Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	Info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	Info.module = Collection.GetShaderModule(VS);
	Info.pName = Collection.GetEntryPoint(VS).c_str();
	OutShaderStages.push_back(Info);

	VkShaderModule PixelShaderModule = Collection.GetShaderModule(PS);
	if (PixelShaderModule != VK_NULL_HANDLE)
	{
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		Info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		Info.module = PixelShaderModule;
		Info.pName = Collection.GetEntryPoint(PS).c_str();
		OutShaderStages.push_back(Info);
	}
}

bool FComputePSO::Create(VkDevice Device, FShaderHandle InCS)
{
	CS = InCS;
	auto* Shader = Collection.GetVulkanShader(CS);
	check(Shader);
	Shader->GenerateReflection(DescriptorSetInfo);
	CreateDescriptorSetLayout(Device);
	return true;
}
