// Vk.cpp

#include "stdafx.h"
#include "Vk.h"
#include "VkMem.h"
#include "VkResources.h"
#include "../Meshes/ObjLoader.h"


static FInstance GInstance;
static FDevice GDevice;
static FMemManager GMemMgr;
static FCmdBufferMgr GCmdBufferMgr;
static FSwapchain GSwapchain;
static FDescriptorPool GDescriptorPool;
static FStagingManager GStagingManager;


static FBuffer GObjVB;
static Obj::FObj GObj;
static FBuffer GFloorVB;
static FIndexBuffer GFloorIB;

struct FViewUB
{
	FMatrix4x4 View;
	FMatrix4x4 Proj;
};
static FBuffer GViewUB;

struct FObjUB
{
	FMatrix4x4 Obj;
};
static FBuffer GObjUB;
static FBuffer GIdentityUB;

static FImage2DWithView GCheckerboardTexture;
static FImage2DWithView GSceneColor;
static FImage2DWithView GSceneColorAfterPost;
static FImage2DWithView GDepthBuffer;
static FImage2DWithView GHeightMap;
static FSampler GSampler;



struct FPosColorUVVertex
{
	float x, y, z;
	uint32 Color;
	float u, v;
};
FVertexFormat GPosColorUVFormat;

bool GQuitting = false;

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

	FGfxPipeline* GetOrCreateGfxPipeline(FGfxPSO* GfxPSO, FVertexFormat* VF, uint32 Width, uint32 Height, VkRenderPass RenderPass)
	{
		FGfxPSOLayout Layout(GfxPSO, VF, Width, Height, RenderPass);
		auto Found = GfxPipelines.find(Layout);
		if (Found != GfxPipelines.end())
		{
			return Found->second;
		}

		auto* NewPipeline = new FGfxPipeline;
		NewPipeline->Create(Device->Device, GfxPSO, VF, Width, Height, RenderPass);
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
void MapAndFillBufferSyncOneShotCmdBuffer(FBuffer* DestBuffer, TFillLambda Fill, uint32 Size)
{
	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();
	FStagingBuffer* StagingBuffer = GStagingManager.RequestUploadBuffer(Size);
	MapAndFillBufferSync(StagingBuffer, CmdBuffer, DestBuffer, Fill, Size);
	FlushMappedBuffer(GDevice.Device, StagingBuffer);
	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();
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

	// Setup Vertex Format
	GPosColorUVFormat.AddVertexBuffer(0, sizeof(FPosColorUVVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	GPosColorUVFormat.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FPosColorUVVertex, x));
	GPosColorUVFormat.AddVertexAttribute(0, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FPosColorUVVertex, Color));
	GPosColorUVFormat.AddVertexAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(FPosColorUVVertex, u));

	// Load and fill geometry
	if (!Obj::Load("../Meshes/Cube/cube.obj", GObj))
	{
		return false;
	}
	//GObj.Faces.resize(1);
	GObjVB.Create(GDevice.Device, sizeof(FPosColorUVVertex) * GObj.Faces.size() * 3, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);

	auto FillObj = [](void* Data)
	{
		check(Data);
		auto* Vertex = (FPosColorUVVertex*)Data;
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

	MapAndFillBufferSyncOneShotCmdBuffer(&GObjVB, FillObj, sizeof(FPosColorUVVertex) * GObj.Faces.size() * 3);

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
		auto* StagingBuffer = GStagingManager.RequestUploadBufferForImage(&GHeightMap.Image);
		MapAndFillImageSync(StagingBuffer, CmdBuffer, &GHeightMap.Image, FillHeightMap);
		FlushMappedBuffer(GDevice.Device, StagingBuffer);

		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GHeightMap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	CmdBuffer->End();
	GCmdBufferMgr.Submit(CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();
}

static void SetupFloor()
{
	GFloorVB.Create(GDevice.Device, sizeof(FPosColorUVVertex) * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr);
	auto FillVertices = [](void* Data)
	{
		check(Data);
		auto* Vertex = (FPosColorUVVertex*)Data;
		float Y = 10;
		float Extent = 250;
		Vertex[0].x = -Extent; Vertex[0].y = Y; Vertex[0].z = -Extent; Vertex[0].Color = 0xffff0000; Vertex[0].u = 0; Vertex[0].v = 0;
		Vertex[1].x = Extent; Vertex[1].y = Y; Vertex[1].z = -Extent; Vertex[1].Color = 0xff00ff00; Vertex[1].u = 1; Vertex[1].v = 0;
		Vertex[2].x = Extent; Vertex[2].y = Y; Vertex[2].z = Extent; Vertex[2].Color = 0xff0000ff; Vertex[2].u = 1; Vertex[2].v = 1;
		Vertex[3].x = -Extent; Vertex[3].y = Y; Vertex[3].z = Extent; Vertex[3].Color = 0xffff00ff; Vertex[3].u = 0; Vertex[3].v = 1;
	};
	MapAndFillBufferSyncOneShotCmdBuffer(&GFloorVB, FillVertices, sizeof(FPosColorUVVertex) * 4);

	GFloorIB.Create(GDevice.Device, 4, VK_INDEX_TYPE_UINT32, &GMemMgr);
	auto FillIndices = [](void* Data)
	{
		check(Data);
		auto* Index = (uint32*)Data;
		Index[0] = 0;
		Index[1] = 1;
		Index[2] = 2;
		Index[3] = 3;
	};
	MapAndFillBufferSyncOneShotCmdBuffer(&GFloorIB.Buffer, FillIndices, sizeof(uint32) * 4);
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
	GStagingManager.Create(GDevice.Device, &GMemMgr);

	GObjectCache.Create(&GDevice);

	if (!LoadShadersAndGeometry())
	{
		return false;
	}

	GViewUB.Create(GDevice.Device, sizeof(FViewUB), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);
	GObjUB.Create(GDevice.Device, sizeof(FObjUB), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);
	GIdentityUB.Create(GDevice.Device, sizeof(FObjUB), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &GMemMgr);

	FViewUB& ViewUB = *(FViewUB*)GViewUB.GetMappedData();
	ViewUB.View = FMatrix4x4::GetIdentity();
	ViewUB.View.Values[3 * 4 + 2] = -2;
	ViewUB.Proj = CalculateProjectionMatrix(ToRadians(60), (float)GSwapchain.GetWidth() / (float)GSwapchain.GetHeight(), 0.1f, 1000.0f);

	{
		FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
	}

	{
		FObjUB& ObjUB = *(FObjUB*)GIdentityUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
	}

	CreateAndFillTexture();
	SetupFloor();

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

static void DrawCube(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	FObjUB& ObjUB = *(FObjUB*)GObjUB.GetMappedData();
	static float AngleDegrees = 0;
	{
		AngleDegrees += 360.0f / 10.0f / 60.0f;
		AngleDegrees = fmod(AngleDegrees, 360.0f);
	}
	ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees));

	auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GObjUB);
	WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, /*GCheckerboardTexture*/GHeightMap.ImageView);
	vkUpdateDescriptorSets(Device, WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);

	vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);

	VkDeviceSize Offset = 0;
	vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GObjVB.Buffer, &Offset);
	vkCmdDraw(CmdBuffer->CmdBuffer, GObj.Faces.size() * 3, 1, 0, 0);
}

static void DrawFloor(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	auto DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GIdentityUB);
	WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, GCheckerboardTexture.ImageView);
	vkUpdateDescriptorSets(Device, WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);

	vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);

	VkDeviceSize Offset = 0;
	vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &GFloorVB.Buffer, &Offset);
	GFloorIB.Bind(CmdBuffer->CmdBuffer);
	vkCmdDrawIndexed(CmdBuffer->CmdBuffer, 4, 1, 0, 0, 0);
}

static void SetDynamicStates(VkCommandBuffer CmdBuffer, uint32 Width, uint32 Height)
{
	VkViewport Viewport;
	MemZero(Viewport);
	Viewport.width = (float)Width;
	Viewport.height = (float)Height;
	Viewport.maxDepth = 1;
	vkCmdSetViewport(CmdBuffer, 0, 1, &Viewport);

	VkRect2D Scissor;
	MemZero(Scissor);
	Scissor.extent.width = Width;
	Scissor.extent.height = Height;
	vkCmdSetScissor(CmdBuffer, 0, 1, &Scissor);
}

static void RenderFrame(VkDevice Device, FCmdBuffer* CmdBuffer, uint32 Width, uint32 Height, VkImageView ColorImageView, VkFormat ColorFormat, FImage2DWithView* DepthBuffer)
{
	auto* RenderPass = GObjectCache.GetOrCreateRenderPass(Width, Height, 1, &ColorFormat, DepthBuffer->GetFormat());
	CmdBuffer->BeginRenderPass(RenderPass->RenderPass, *GObjectCache.GetOrCreateFramebuffer(RenderPass->RenderPass, ColorImageView, DepthBuffer->GetImageView(), Width, Height));

	auto* GfxPipeline = GObjectCache.GetOrCreateGfxPipeline(&GTestPSO, &GPosColorUVFormat, Width, Height, RenderPass->RenderPass);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->Pipeline);

	SetDynamicStates(CmdBuffer->CmdBuffer, Width, Height);

	DrawFloor(GfxPipeline, Device, CmdBuffer);
	DrawCube(GfxPipeline, Device, CmdBuffer);

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

	GFloorIB.Destroy();
	GFloorVB.Destroy();
	GViewUB.Destroy();
	GObjUB.Destroy();
	GObjVB.Destroy();
	GIdentityUB.Destroy();

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
	GStagingManager.Destroy();
	GObjectCache.Destroy();
	GCmdBufferMgr.Destroy();
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
