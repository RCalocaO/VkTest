// Vk.cpp

#include "stdafx.h"
#include "Vk.h"
#include "VkMem.h"
#include "VkResources.h"
#include "../Meshes/ObjLoader.h"
#include "VkObj.h"

#define USE_SPONZA 0

// 0 no multithreading
// 1 inside a render pass
// 2 run post
#define TRY_MULTITHREADED	0

FControl::FControl()
	: StepDirection{0, 0, 0}
	, CameraPos{0, 0, -10, 1}
	, ViewMode(EViewMode::Solid)
	, DoPost(!true)
	, DoMSAA(false)
{
}

FControl GRequestControl;
FControl GControl;

FVector4 GCameraPos = {0, 0, -10, 1};

static FInstance GInstance;
static FDevice GDevice;
static FMemManager GMemMgr;
static FCmdBufferMgr GCmdBufferMgr;
static FSwapchain GSwapchain;
static FDescriptorPool GDescriptorPool;
static FStagingManager GStagingManager;

static FMesh GCube;
static FMesh GSponza;
static FVertexBuffer GFloorVB;
static FIndexBuffer GFloorIB;
struct FCreateFloorUB
{
	float Y;
	float Extent;
	uint32 NumQuadsX;
	uint32 NumQuadsZ;
	float Elevation;
};
static FUniformBuffer<FCreateFloorUB> GCreateFloorUB;

struct FViewUB
{
	FMatrix4x4 View;
	FMatrix4x4 Proj;
};
static FUniformBuffer<FViewUB> GViewUB;

struct FObjUB
{
	FMatrix4x4 Obj;
};
static FUniformBuffer<FObjUB> GObjUB;
static FUniformBuffer<FObjUB> GIdentityUB;

static FImage2DWithView GCheckerboardTexture;
static FImage2DWithView GHeightMap;
static FSampler GSampler;
static FImageCubeWithView GCubeTest;

struct FRenderTargetPool
{
	struct FEntry
	{
		bool bFree = true;
		VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		FImage2DWithView Texture;
		const char* Name = nullptr;
		VkImageUsageFlags Usage = 0;
		VkMemoryPropertyFlags MemProperties = 0;

		void DoTransition(FCmdBuffer* CmdBuffer, VkImageLayout NewLayout)
		{
			if (Layout == NewLayout)
			{
				return;
			}

			auto IsDepthStencil = [](VkImageLayout InLayout)
			{
				switch (InLayout)
				{
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
					return true;

				default:
					return false;
				}
			};

			auto GetAccessMask = [](VkImageLayout InLayout) -> int
			{
				switch (InLayout)
				{
				case VK_IMAGE_LAYOUT_UNDEFINED:
					return 0;

				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
					return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				case VK_IMAGE_LAYOUT_GENERAL:
					return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
					return VK_ACCESS_TRANSFER_READ_BIT;

				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
					return VK_ACCESS_TRANSFER_WRITE_BIT;

				default:
					check(0);
					break;
				}

				return 0;
			};
			VkImageAspectFlagBits Aspect = IsDepthStencil(NewLayout) ? (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Texture.GetImage(), Layout, GetAccessMask(Layout), NewLayout, GetAccessMask(NewLayout), Aspect);
			Layout = NewLayout;
		}
	};

	VkDevice Device = VK_NULL_HANDLE;
	FMemManager* MemMgr = nullptr;

	void Create(VkDevice InDevice, FMemManager* InMemMgr)
	{
		Device = InDevice;
		MemMgr = InMemMgr;

		InitializeCriticalSection(&CS);
	}

	void Destroy()
	{
		for (auto* Entry : Entries)
		{
			check(Entry->bFree);
			Entry->Texture.Destroy();
			delete Entry;
		}
	}

	void EmptyPool()
	{
		::EnterCriticalSection(&CS);
		for (auto* Entry : Entries)
		{
			if (!Entry->bFree)
			{
				Release(Entry);
			}
		}
	}

	FEntry* Acquire(const char* InName, uint32 Width, uint32 Height, VkFormat Format, VkImageUsageFlags Usage, VkMemoryPropertyFlagBits MemProperties, uint32 NumMips, VkSampleCountFlagBits Samples)
	{
		::EnterCriticalSection(&CS);
		for (auto* Entry : Entries)
		{
			if (Entry->bFree)
			{
				FImage& Image = Entry->Texture.Image;
				if (Image.Width == Width && 
					Image.Height == Height &&
					Image.Format == Format &&
					Image.NumMips == NumMips &&
					Image.Samples == Samples &&
					Entry->Usage == Usage &&
					Entry->MemProperties == MemProperties
					)
				{
					Entry->bFree = false;
					//Entry->Layout = VK_IMAGE_LAYOUT_UNDEFINED;
					Entry->Name = InName;
					return Entry;
				}
			}
		}

		FEntry* Entry = new FEntry;
		Entries.push_back(Entry);
		::LeaveCriticalSection(&CS);

		Entry->bFree = false;
		Entry->Usage = Usage;
		Entry->MemProperties = MemProperties;
		Entry->Name = InName;

		Entry->Texture.Create(Device, Width, Height, Format, Usage, MemProperties, MemMgr, NumMips, Samples);

		return Entry;
	}

	void Release(FEntry*& Entry)
	{
		::EnterCriticalSection(&CS);
		check(!Entry->bFree);
		Entry->bFree = true;
		::LeaveCriticalSection(&CS);
		Entry = nullptr;
	}

	std::vector<FEntry*> Entries;

	CRITICAL_SECTION CS;
};
static FRenderTargetPool GRenderTargetPool;


#if TRY_MULTITHREADED > 0
struct FThread
{
	volatile FPrimaryCmdBuffer* ParentCmdBuffer = nullptr;
	volatile HANDLE StartEvent = INVALID_HANDLE_VALUE;
	volatile HANDLE DoneEvent = INVALID_HANDLE_VALUE;
	volatile bool bDoQuit = false;
	volatile int32 Width = 0;
	volatile int32 Height = 0;
	volatile FRenderPass* RenderPass = nullptr;
	volatile FFramebuffer* Framebuffer = nullptr;

	void Create()
	{
		ThreadHandle = ::CreateThread(nullptr, 0, ThreadFunction, this, 0, &ThreadId);
		StartEvent = ::CreateEventA(nullptr, true, false, "StartEvent");
		DoneEvent = ::CreateEventA(nullptr, true, false, "DoneEvent");
	}

	DWORD ThreadId = 0;
	HANDLE ThreadHandle = INVALID_HANDLE_VALUE;

	static DWORD __stdcall ThreadFunction(void*);
};
FThread GThread;

#endif

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
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FOneImagePSO GFillTexturePSO;
FOneImagePSO GTestComputePSO;

struct FTestPostComputePSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FTestPostComputePSO GTestComputePostPSO;

struct FSetupFloorPSO : public FComputePSO
{
	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		AddBinding(OutBindings, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	}
};
static FSetupFloorPSO GSetupFloorPSO;


struct FObjectCache
{
	FDevice* Device = nullptr;

	std::map<uint64, FRenderPass*> RenderPasses;
	std::map<FComputePSO*, FComputePipeline*> ComputePipelines;
	std::map<FGfxPSOLayout, FGfxPipeline*> GfxPipelines;

	struct FFrameBufferEntry
	{
		FFramebuffer* Framebuffer;

		FFrameBufferEntry(VkRenderPass InRenderPass, uint32 InWidth, uint32 InHeight, uint32 InNumColorTargets, VkImageView* InColorViews, VkImageView InDepthStencilView, VkImageView InResolveColor, VkImageView InResolveDepth)
			: RenderPass(InRenderPass)
			, Width(InWidth)
			, Height(InHeight)
			, NumColorTargets(InNumColorTargets)
			, DepthStencilView(InDepthStencilView)
			, ResolveColor(InResolveColor)
			, ResolveDepth(InResolveDepth)
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
		VkImageView ResolveColor = VK_NULL_HANDLE;
		VkImageView ResolveDepth = VK_NULL_HANDLE;
	};
	std::vector<FFrameBufferEntry> Framebuffers;

	void Create(FDevice* InDevice)
	{
		Device = InDevice;
	}

	FFramebuffer* GetOrCreateFramebuffer(VkRenderPass RenderPass, VkImageView Color, VkImageView DepthStencil, uint32 Width, uint32 Height, VkImageView ResolveColor = VK_NULL_HANDLE, VkImageView ResolveDepth = VK_NULL_HANDLE)
	{
		for (auto& Entry : Framebuffers)
		{
			if (Entry.RenderPass == RenderPass && Entry.NumColorTargets == 1 && Entry.ColorViews[0] == Color && Entry.DepthStencilView == DepthStencil && Entry.Width == Width && Entry.Height == Height && Entry.ResolveColor == ResolveColor && Entry.ResolveDepth == ResolveDepth)
			{
				return Entry.Framebuffer;
			}
		}

		FFrameBufferEntry Entry(RenderPass, Width, Height, 1, &Color, DepthStencil, ResolveColor, ResolveDepth);

		auto* NewFramebuffer = new FFramebuffer;
		NewFramebuffer->Create(Device->Device, RenderPass, Color, DepthStencil, Width, Height, ResolveColor, ResolveDepth);
		Entry.Framebuffer = NewFramebuffer;
		Framebuffers.push_back(Entry);
		return NewFramebuffer;
	}

	FGfxPipeline* GetOrCreateGfxPipeline(const FGfxPSOLayout& Layout)
	{
		auto Found = GfxPipelines.find(Layout);
		if (Found != GfxPipelines.end())
		{
			return Found->second;
		}

		auto* NewPipeline = new FGfxPipeline;
		NewPipeline->RSInfo.polygonMode = Layout.bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		switch (Layout.Blend)
		{
		case FGfxPSOLayout::EBlend::Translucent:
			for (uint32 Index = 0; Index < NewPipeline->CBInfo.attachmentCount; ++Index)
			{
				VkPipelineColorBlendAttachmentState* Attachment = (VkPipelineColorBlendAttachmentState*)&NewPipeline->CBInfo.pAttachments[Index];
				//Attachment->blendEnable = VK_TRUE;
				Attachment->colorBlendOp = VK_BLEND_OP_ADD;
				Attachment->alphaBlendOp = VK_BLEND_OP_ADD;
				Attachment->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
				Attachment->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				Attachment->srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				Attachment->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			}
			break;
		default:
			break;
		}
		NewPipeline->Create(Device->Device, Layout.GfxPSO, Layout.VF, Layout.Width, Layout.Height, Layout.RenderPass);
		GfxPipelines[Layout] = NewPipeline;
		return NewPipeline;
	}

	FGfxPipeline* GetOrCreateGfxPipeline(FGfxPSO* GfxPSO, FVertexFormat* VF, uint32 Width, uint32 Height, FRenderPass* RenderPass, bool bWireframe = false)
	{
		FGfxPSOLayout Layout(GfxPSO, VF, Width, Height, RenderPass, bWireframe);
		return GetOrCreateGfxPipeline(Layout);
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

	FRenderPass* GetOrCreateRenderPass(uint32 Width, uint32 Height, uint32 NumColorTargets, VkFormat* ColorFormats, VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED, VkSampleCountFlagBits InNumSamples = VK_SAMPLE_COUNT_1_BIT, FImage2DWithView* ResolveColorBuffer = nullptr, FImage2DWithView* ResolveDepth = nullptr)
	{
		FRenderPassLayout Layout(Width, Height, NumColorTargets, ColorFormats, DepthStencilFormat, InNumSamples, ResolveColorBuffer ? ResolveColorBuffer->GetFormat() : VK_FORMAT_UNDEFINED, ResolveDepth ? ResolveDepth->GetFormat() : VK_FORMAT_UNDEFINED);
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

		if (!DoCompile(" ../Shaders/CreateFloor.comp"))
		{
			return false;
		}

		check(GTestPSO.CreateVSPS(GDevice.Device, "vert.spv", "frag.spv"));
		check(GTestComputePSO.Create(GDevice.Device, "comp.spv"));
		check(GTestComputePostPSO.Create(GDevice.Device, "TestPost.spv"));
		check(GFillTexturePSO.Create(GDevice.Device, "FillTexture.spv"));
		check(GSetupFloorPSO.Create(GDevice.Device, "CreateFloor.spv"));
	}
	else
	{
		check(GSetupFloorPSO.Create(GDevice.Device, "../Shaders/CreateFloor.comp.spv"));
		check(GTestComputePostPSO.Create(GDevice.Device, "../Shaders/TestPost.comp.spv"));
		check(GFillTexturePSO.Create(GDevice.Device, "../Shaders/FillTexture.comp.spv"));
		check(GTestPSO.CreateVSPS(GDevice.Device, "../Shaders/Test0.vert.spv", "../Shaders/Test0.frag.spv"));
		check(GTestComputePSO.Create(GDevice.Device, "../Shaders/Test0.comp.spv"));
	}

	// Setup Vertex Format
	GPosColorUVFormat.AddVertexBuffer(0, sizeof(FPosColorUVVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	GPosColorUVFormat.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FPosColorUVVertex, x));
	GPosColorUVFormat.AddVertexAttribute(0, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FPosColorUVVertex, Color));
	GPosColorUVFormat.AddVertexAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(FPosColorUVVertex, u));

	// Load and fill geometry
	if (!GCube.Load("../Meshes/cube/cube.obj"))
	{
		return false;
	}
	GCube.Create(&GDevice, &GCmdBufferMgr, &GStagingManager, &GMemMgr);

#if USE_SPONZA
	if (!GSponza.Load("../Meshes/sponza/sponza.obj"))
	{
		return false;
	}
	GSponza.Create(&GDevice, &GCmdBufferMgr, &GStagingManager, &GMemMgr);
#endif

	return true;
}

void CreateAndFillTexture()
{
	srand(0);
	GCheckerboardTexture.Create(GDevice.Device, 64, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);
	GHeightMap.Create(GDevice.Device, 64, 64, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);

	GCubeTest.Create(GDevice.Device, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);

	FComputePipeline* Pipeline = GObjectCache.GetOrCreateComputePipeline(&GFillTexturePSO);

	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GCheckerboardTexture.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Pipeline);
	{
		auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GFillTexturePSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddStorageImage(DescriptorSet, 0, GCheckerboardTexture.ImageView);
		GDescriptorPool.UpdateDescriptors(WriteDescriptors);
		//vkUpdateDescriptorSets(GDevice.Device, (uint32)WriteDescriptors.DSWrites.size(), &WriteDescriptors.DSWrites[0], 0, nullptr);
		DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
		//vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->PipelineLayout, 0, 1, &DescriptorSet.Set, 0, nullptr);
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
	GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();
}


static void FillFloor(FCmdBuffer* CmdBuffer)
{
	BufferBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, &GFloorVB.Buffer, 0, VK_ACCESS_SHADER_WRITE_BIT);
	BufferBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, &GFloorIB.Buffer, 0, VK_ACCESS_SHADER_WRITE_BIT);
	auto* ComputePipeline = GObjectCache.GetOrCreateComputePipeline(&GSetupFloorPSO);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->Pipeline);

	FCreateFloorUB& CreateFloorUB = *GCreateFloorUB.GetMappedData();

	{
		auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GSetupFloorPSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddStorageBuffer(DescriptorSet, 0, GFloorIB.Buffer);
		WriteDescriptors.AddStorageBuffer(DescriptorSet, 1, GFloorVB.Buffer);
		WriteDescriptors.AddUniformBuffer(DescriptorSet, 2, GCreateFloorUB);
		WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 3, GSampler, GHeightMap.ImageView);
		GDescriptorPool.UpdateDescriptors(WriteDescriptors);
		DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, CreateFloorUB.NumQuadsX, 1, CreateFloorUB.NumQuadsZ);
	BufferBarrier(CmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, &GFloorIB.Buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
	BufferBarrier(CmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, &GFloorVB.Buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
}

static void SetupFloor()
{
/*
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
	MapAndFillBufferSyncOneShotCmdBuffer(&GFloorVB.Buffer, FillVertices, sizeof(FPosColorUVVertex) * 4);
*/
	uint32 NumQuadsX = 128;
	uint32 NumQuadsZ = 128;
	float Elevation = 40;
	GCreateFloorUB.Create(GDevice.Device, &GMemMgr);
	{
		FCreateFloorUB& CreateFloorUB = *GCreateFloorUB.GetMappedData();
		CreateFloorUB.Y = 10;
		CreateFloorUB.Extent = 250;
		CreateFloorUB.NumQuadsX = NumQuadsX;
		CreateFloorUB.NumQuadsZ = NumQuadsZ;
		CreateFloorUB.Elevation = Elevation;
	}

	GFloorVB.Create(GDevice.Device, sizeof(FPosColorUVVertex) * 4 * NumQuadsX * NumQuadsZ, &GMemMgr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	GFloorIB.Create(GDevice.Device, 3 * 2 * (NumQuadsX - 1) * (NumQuadsZ - 1), VK_INDEX_TYPE_UINT32, &GMemMgr, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	{
		auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
		CmdBuffer->Begin();
		FillFloor(CmdBuffer);
		CmdBuffer->End();
		GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
		CmdBuffer->WaitForFence();
	}
/*
	auto FillIndices = [](void* Data)
	{
		check(Data);
		auto* Index = (uint32*)Data;
		Index[0] = 0;
		Index[1] = 1;
		Index[2] = 2;
		Index[3] = 3;
	};
	MapAndFillBufferSyncOneShotCmdBuffer(&GFloorIB.Buffer, FillIndices, sizeof(uint32) * 4);*/
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

	GViewUB.Create(GDevice.Device, &GMemMgr);
	GObjUB.Create(GDevice.Device, &GMemMgr);
	GIdentityUB.Create(GDevice.Device, &GMemMgr);

	{
		FObjUB& ObjUB = *GObjUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
	}

	{
		FObjUB& ObjUB = *GIdentityUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
	}

	GRenderTargetPool.Create(GDevice.Device, &GMemMgr);

	CreateAndFillTexture();

	GSampler.Create(GDevice.Device);
	SetupFloor();

	{
		// Setup on Present layout
		auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
		CmdBuffer->Begin();
		GSwapchain.ClearAndTransitionToPresent(CmdBuffer);
		CmdBuffer->End();
		GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
		CmdBuffer->WaitForFence();
	}

#if TRY_MULTITHREADED
	GThread.Create();
#endif

	return true;
}

static void DrawCube(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	FObjUB& ObjUB = *GObjUB.GetMappedData();
	static float AngleDegrees = 0;
	{
		AngleDegrees += 360.0f / 10.0f / 60.0f;
		AngleDegrees = fmod(AngleDegrees, 360.0f);
	}
	ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees));

	auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GObjUB);
	WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, /*GCheckerboardTexture*/GHeightMap.ImageView);
	GDescriptorPool.UpdateDescriptors(WriteDescriptors);

	DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);

	CmdBind(CmdBuffer, &GCube.ObjVB);
	vkCmdDraw(CmdBuffer->CmdBuffer, GCube.GetNumVertices(), 1, 0, 0);
}

static void DrawSponza(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	FObjUB& ObjUB = *GObjUB.GetMappedData();
	ObjUB.Obj = FMatrix4x4::GetIdentity();

	auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GObjUB);
	WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, /*GCheckerboardTexture*/GHeightMap.ImageView);
	GDescriptorPool.UpdateDescriptors(WriteDescriptors);

	DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);

	CmdBind(CmdBuffer, &GSponza.ObjVB);
	vkCmdDraw(CmdBuffer->CmdBuffer, GSponza.GetNumVertices(), 1, 0, 0);
}

static void DrawFloor(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GIdentityUB);
	WriteDescriptors.AddCombinedImageSampler(DescriptorSet, 2, GSampler, GCheckerboardTexture.ImageView);
	GDescriptorPool.UpdateDescriptors(WriteDescriptors);
	DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);

	CmdBind(CmdBuffer, &GFloorVB);
	CmdBind(CmdBuffer, &GFloorIB);
	vkCmdDrawIndexed(CmdBuffer->CmdBuffer, GFloorIB.NumIndices, 1, 0, 0, 0);
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

static void UpdateCamera()
{
	FViewUB& ViewUB = *GViewUB.GetMappedData();
	ViewUB.View = FMatrix4x4::GetIdentity();
	//ViewUB.View.Values[3 * 4 + 2] = -10;
	GCameraPos = GCameraPos.Add(GControl.StepDirection.Mul(0.01f));
	GRequestControl.StepDirection = {0, 0, 0};
	GControl.StepDirection ={0, 0, 0};
	ViewUB.View.Rows[3] = GCameraPos;
	ViewUB.Proj = CalculateProjectionMatrix(ToRadians(60), (float)GSwapchain.GetWidth() / (float)GSwapchain.GetHeight(), 0.1f, 1000.0f);
}


static void InternalRenderFrame(VkDevice Device, FRenderPass* RenderPass, FCmdBuffer* CmdBuffer, uint32 Width, uint32 Height)
{
	auto* GfxPipeline = GObjectCache.GetOrCreateGfxPipeline(&GTestPSO, &GPosColorUVFormat, Width, Height, RenderPass, GControl.ViewMode == EViewMode::Wireframe);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->Pipeline);

	SetDynamicStates(CmdBuffer->CmdBuffer, Width, Height);

#if USE_SPONZA
	DrawSponza(GfxPipeline, Device, CmdBuffer);
#else
	DrawFloor(GfxPipeline, Device, CmdBuffer);
	DrawCube(GfxPipeline, Device, CmdBuffer);
#endif
}

static void RenderFrame(VkDevice Device, FPrimaryCmdBuffer* CmdBuffer, FImage2DWithView* ColorBuffer, FImage2DWithView* DepthBuffer, FImage2DWithView* ResolveColorBuffer, FImage2DWithView* ResolveDepth)
{
	UpdateCamera();

	FillFloor(CmdBuffer);

	VkFormat ColorFormat = ColorBuffer->GetFormat();
	auto* RenderPass = GObjectCache.GetOrCreateRenderPass(ColorBuffer->GetWidth(), ColorBuffer->GetHeight(), 1, &ColorFormat, DepthBuffer->GetFormat(), ColorBuffer->Image.Samples, ResolveColorBuffer, ResolveDepth);
	auto* Framebuffer = GObjectCache.GetOrCreateFramebuffer(RenderPass->RenderPass, ColorBuffer->GetImageView(), DepthBuffer->GetImageView(), ColorBuffer->GetWidth(), ColorBuffer->GetHeight(), ResolveColorBuffer ? ResolveColorBuffer->GetImageView() : VK_NULL_HANDLE, ResolveDepth ? ResolveDepth->GetImageView() : VK_NULL_HANDLE);

	CmdBuffer->BeginRenderPass(RenderPass->RenderPass, *Framebuffer, TRY_MULTITHREADED == 1);
#if TRY_MULTITHREADED == 1
	{
		GThread.ParentCmdBuffer = CmdBuffer;
		GThread.Width = ColorBuffer->GetWidth();
		GThread.Height = ColorBuffer->GetHeight();
		GThread.RenderPass = RenderPass;
		GThread.Framebuffer = Framebuffer;
		ResetEvent(GThread.DoneEvent);
		SetEvent(GThread.StartEvent);
		WaitForSingleObject(GThread.DoneEvent, INFINITE);
		CmdBuffer->ExecuteSecondary();
	}
#else
	InternalRenderFrame(Device, RenderPass, CmdBuffer, ColorBuffer->GetWidth(), ColorBuffer->GetHeight());
#endif

	CmdBuffer->EndRenderPass();
}

void RenderPost(VkDevice Device, FCmdBuffer* CmdBuffer, FRenderTargetPool::FEntry* SceneColorEntry, FRenderTargetPool::FEntry* SceneColorAfterPostEntry)
{
	SceneColorEntry->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_GENERAL);
	SceneColorAfterPostEntry->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_GENERAL);
	auto* ComputePipeline = GObjectCache.GetOrCreateComputePipeline(&GTestComputePostPSO);

	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline->Pipeline);

	{
		auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestComputePSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddStorageImage(DescriptorSet, 0, SceneColorEntry->Texture.ImageView);
		WriteDescriptors.AddStorageImage(DescriptorSet, 1, SceneColorAfterPostEntry->Texture.ImageView);
		GDescriptorPool.UpdateDescriptors(WriteDescriptors);
		DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ComputePipeline);
	}

	vkCmdDispatch(CmdBuffer->CmdBuffer, SceneColorAfterPostEntry->Texture.Image.Width / 8, SceneColorAfterPostEntry->Texture.Image.Height / 8, 1);
}

#if TRY_MULTITHREADED
DWORD __stdcall FThread::ThreadFunction(void* Param)
{
	auto* This = (FThread*)Param;

	FCmdBufferMgr ThreadMgr;
	ThreadMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);
	while (!This->bDoQuit)
	{
		WaitForSingleObject(This->StartEvent, INFINITE);
		if (This->bDoQuit)
		{
			break;
		}

		VkFormat ColorFormat = (VkFormat)GSwapchain.BACKBUFFER_VIEW_FORMAT;
		FPrimaryCmdBuffer* ParentCmdBuffer = (FPrimaryCmdBuffer*)This->ParentCmdBuffer;
		auto* CmdBuffer = ThreadMgr.AllocateSecondaryCmdBuffer(ParentCmdBuffer->Fence);
		CmdBuffer->BeginSecondary(ParentCmdBuffer, This->RenderPass ? This->RenderPass->RenderPass : VK_NULL_HANDLE, This->Framebuffer ? This->Framebuffer->Framebuffer : VK_NULL_HANDLE);

#if TRY_MULTITHREADED == 1
		InternalRenderFrame(GDevice.Device, (FRenderPass*)This->RenderPass, CmdBuffer, This->Width, This->Height);
#elif TRY_MULTITHREADED == 2
		RenderPost(GDevice.Device, CmdBuffer, &GSceneColor, &GSceneColorAfterPost);
#endif

		CmdBuffer->End();

		//::Sleep(0);
		//This->bWorkDone = true;
		ResetEvent(This->StartEvent);
		SetEvent(This->DoneEvent);
	}

	ThreadMgr.Destroy();

	return 0;
}
#endif

void DoRender()
{
	GRenderTargetPool.EmptyPool();

	if (GQuitting)
	{
		return;
	}

	GControl = GRequestControl;

	auto* CmdBuffer = GCmdBufferMgr.GetActivePrimaryCmdBuffer();
	CmdBuffer->Begin();

	auto* SceneColor = GRenderTargetPool.Acquire(GControl.DoMSAA ? "SceneColorMSAA" : "SceneColor", GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, GControl.DoMSAA ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT);

	SceneColor->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	//TestCompute(CmdBuffer);

	VkFormat ColorFormat = GSwapchain.Format;

	auto* DepthBuffer = GRenderTargetPool.Acquire("DepthBuffer", GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, SceneColor->Texture.Image.Samples);
	DepthBuffer->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	if (GControl.DoMSAA)
	{
/*
		VkImageResolve Region;
		MemZero(Region);
		Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.srcSubresource.layerCount = 1;
		Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.dstSubresource.layerCount = 1;
		Region.extent.width = SceneColor->Texture.GetWidth();
		Region.extent.height = SceneColor->Texture.GetHeight();
		Region.extent.depth = 1;

		auto* MSAA = SceneColor;
		SceneColor = GRenderTargetPool.Acquire("SceneColor", GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
		vkCmdResolveImage(CmdBuffer->CmdBuffer, MSAA->Texture.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, SceneColor->Texture.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, &Region);
*/
		auto* ResolvedSceneColor = GRenderTargetPool.Acquire("SceneColor", GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
		ResolvedSceneColor->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		auto* ResolvedDepth = GRenderTargetPool.Acquire("Depth", GSwapchain.GetWidth(), GSwapchain.GetHeight(), DepthBuffer->Texture.GetFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
		ResolvedDepth->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		RenderFrame(GDevice.Device, CmdBuffer, &SceneColor->Texture, &DepthBuffer->Texture, &ResolvedSceneColor->Texture, &ResolvedDepth->Texture);

		auto* MSAA = SceneColor;
		GRenderTargetPool.Release(MSAA);
		SceneColor = ResolvedSceneColor;
		MSAA = DepthBuffer;
		GRenderTargetPool.Release(MSAA);
		DepthBuffer = ResolvedDepth;
	}
	else
	{
		RenderFrame(GDevice.Device, CmdBuffer, &SceneColor->Texture, &DepthBuffer->Texture, nullptr, nullptr);
	}
	GRenderTargetPool.Release(DepthBuffer);

	if (GControl.DoPost)
	{
#if TRY_MULTITHREADED == 2
		check(0);
		GThread.ParentCmdBuffer = CmdBuffer;
		GThread.Width = 0;
		GThread.Height = 0;
		GThread.RenderPass = nullptr;
		GThread.Framebuffer = nullptr;
		ResetEvent(GThread.DoneEvent);
		SetEvent(GThread.StartEvent);
		WaitForSingleObject(GThread.DoneEvent, INFINITE);
		CmdBuffer->ExecuteSecondary();
#else
		auto* PrePost = SceneColor;
		SceneColor = GRenderTargetPool.Acquire("SceneColor", GSwapchain.GetWidth(), GSwapchain.GetHeight(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, VK_SAMPLE_COUNT_1_BIT);
		SceneColor->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		RenderPost(GDevice.Device, CmdBuffer, PrePost, SceneColor);
#endif
	}

	// Blit post into scene color
	GSwapchain.AcquireNextImage();
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		uint32 Width = min(GSwapchain.GetWidth(), SceneColor->Texture.GetWidth());
		uint32 Height = min(GSwapchain.GetHeight(), SceneColor->Texture.GetHeight());
		SceneColor->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		BlitColorImage(CmdBuffer, Width, Height, SceneColor->Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_UNDEFINED);
		GRenderTargetPool.Release(SceneColor);
	}
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	CmdBuffer->End();

	// First submit needs to wait for present semaphore
	GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, &GSwapchain.PresentCompleteSemaphores[GSwapchain.PresentCompleteSemaphoreIndex], &GSwapchain.RenderingSemaphores[GSwapchain.AcquiredImageIndex]);

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

		{
			// Setup on Present layout
			auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
			CmdBuffer->Begin();
			GSwapchain.ClearAndTransitionToPresent(CmdBuffer);
			CmdBuffer->End();
			GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
			CmdBuffer->WaitForFence();
		}
	}
}

void DoDeinit()
{
	checkVk(vkDeviceWaitIdle(GDevice.Device));
	GRenderTargetPool.EmptyPool();
#if TRY_MULTITHREADED
	GThread.bDoQuit = true;
	SetEvent(GThread.StartEvent);
	WaitForMultipleObjects(1, &GThread.ThreadHandle, TRUE, INFINITE);
#endif
	GQuitting = true;

	GFloorIB.Destroy();
	GFloorVB.Destroy();
	GViewUB.Destroy();
	GCreateFloorUB.Destroy();
	GObjUB.Destroy();
	GCube.Destroy();
#if USE_SPONZA
	GSponza.Destroy();
#endif
	GIdentityUB.Destroy();

	GSampler.Destroy();

	GCheckerboardTexture.Destroy();
	GHeightMap.Destroy();
	GCubeTest.Destroy();

	GDescriptorPool.Destroy();

	GTestComputePostPSO.Destroy(GDevice.Device);
	GTestComputePSO.Destroy(GDevice.Device);
	GTestPSO.Destroy(GDevice.Device);
	GSetupFloorPSO.Destroy(GDevice.Device);
	GFillTexturePSO.Destroy(GDevice.Device);

	GRenderTargetPool.Destroy();

	GSwapchain.Destroy();
	GStagingManager.Destroy();
	GObjectCache.Destroy();
	GCmdBufferMgr.Destroy();
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
