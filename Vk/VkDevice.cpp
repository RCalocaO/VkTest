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
