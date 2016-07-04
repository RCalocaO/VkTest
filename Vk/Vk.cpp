// Vk.cpp

#include "stdafx.h"
#include "Vk.h"

static void GetInstanceLayersAndExtensions(std::vector<const char*>& OutLayers, std::vector<const char*>& OutExtensions)
{
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
		DeviceInfo.enabledLayerCount = Layers.size();
		DeviceInfo.ppEnabledLayerNames = &Layers[0];
		DeviceInfo.enabledExtensionCount = 1;
		DeviceInfo.ppEnabledExtensionNames = DeviceExtensions;
		DeviceInfo.pEnabledFeatures = &DeviceFeatures;
		checkVk(vkCreateDevice(PhysicalDevice, &DeviceInfo, nullptr, &Device));
	}

	void Destroy()
	{
		vkDestroyDevice(Device, nullptr);
		Device = VK_NULL_HANDLE;
	}
};
FDevice GDevice;

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

		InstanceInfo.enabledLayerCount = InstanceLayers.size();
		InstanceInfo.ppEnabledLayerNames = &InstanceLayers[0];
		InstanceInfo.enabledExtensionCount = InstanceExtensions.size();
		InstanceInfo.ppEnabledExtensionNames = &InstanceExtensions[0];

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

struct FSwapchain
{
	void Create(VkPhysicalDevice PhysicalDevice, VkDevice Device, VkSurfaceKHR Surface, uint32& WindowWidth, uint32& WindowHeight)
	{
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

		VkExtent2D SurfaceResolution = SurfaceCapabilities.currentExtent;
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
	}

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;

	void Destroy(VkDevice Device)
	{
		vkDestroySwapchainKHR(Device, Swapchain, nullptr);
		Swapchain = VK_NULL_HANDLE;
	}
};
FSwapchain GSwapchain;

#if 0
void CreateSwapchain(VkPhysicalDevice PhysicalDevice)
{
	vkCreateSwapchainKHR();
	vkGetSwapchainImagesKHR();
	vkCreateImageView();

	//Depth
	vkCreateImage();
	vkAllocateMemory();
	vkBindImageMemory();
	vkCreateImageView();
}
#endif

void CreateCmdBuffers()
{
#if 0
	vkCreateCommandPool();
	vkAllocateCommandBuffers();
#endif
}

void CreateVertexBuffers()
{
#if 0
	vkCreateBuffer();
	vkAllocateMemory();
	vkMapMemory();
	vkBindBufferMemory();
	vkCreateBufferView();
#endif
}

void DoInit(HINSTANCE hInstance, HWND hWnd, uint32& Width, uint32& Height)
{
	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice();

	GSwapchain.Create(GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	CreateCmdBuffers();
	CreateVertexBuffers();
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
}

void DoResize(int32 Width, int32 Height)
{
}

void DoDeinit()
{
	GSwapchain.Destroy(GDevice.Device);
	GDevice.Destroy();
	GInstance.Destroy();
}
