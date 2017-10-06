// Vk.cpp

#include "stdafx.h"
#include "Vk.h"
#include "VkMem.h"
#include "VkResources.h"
#include "../Meshes/ObjLoader.h"
#include "VkObj.h"

#include "../Utils/glm/glm/vec4.hpp"
#include "../Utils/glm/glm/mat4x4.hpp"
#include "../Utils/glm/glm/gtc/matrix_transform.hpp"

extern std::string GModelName;
extern bool GRenderDoc;
extern bool GVkTrace;
extern bool GValidation;

enum
{
	NUM_CUBES_X = 4,
	NUM_CUBES_Y = 4,
	NUM_CUBES = NUM_CUBES_X * NUM_CUBES_Y,
};

// 0 no multithreading
// 1 inside a render pass
// 2 run post
#define TRY_MULTITHREADED	0

FControl::FControl()
	: StepDirection{0, 0, 0}
	, CameraPos{-16, 0, -50, 1}
	, ViewMode(EViewMode::Solid)
	, DoPost(!true)
	, DoMSAA(false)
{
}

FControl GRequestControl;
FControl GControl;
const float PI = 3.14159265358979323846f;

struct FCamera
{
	FVector3 Pos;
	float YRotation = 0;
	float XRotation = 0;
	float FOV = 45;

	FCamera()
	{
		Pos.Set(0, 0, -10);
	}
	
	FMatrix4x4 GetViewMatrix()
	{
		glm::mat4 View;
		View = glm::rotate(View, YRotation, glm::vec3(-1.0f, 0.0f, 0.0f));
		View = glm::rotate(View, XRotation, glm::vec3(0.0f, 1.0f, 0.0f));
		FMatrix4x4 M = FMatrix4x4::GetIdentity();
		M = *(FMatrix4x4*)&View;
		/*
		float Yaw = YRotation;
		float Pitch = XRotation;
		M.Rows[0].Set(cos(Yaw), 0, -sin(Yaw), 0);
		M.Rows[1].Set(sin(Yaw) * sin(Pitch), cos(Pitch), cos(Yaw) * sin(Pitch), 0);
		M.Rows[2].Set(sin(Yaw) * cos(Pitch), -sin(Pitch), cos(Pitch) * cos(Yaw), 0);
		M = M.GetTranspose();
*/
		return M;
	}

};
static FCamera GCamera;

static FInstance GInstance;
static FDevice GDevice;
static FMemManager GMemMgr;
static FCmdBufferMgr GCmdBufferMgr;
static FSwapchain GSwapchain;
static FDescriptorPool GDescriptorPool;
static FStagingManager GStagingManager;
static FQueryMgr GQueryMgr;
static FVulkanShaderCollection GShaderCollection;

static FMesh GCube;
static std::vector<FMeshInstance> GCubeInstances;
static FMesh GModel;
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
//static FUniformBuffer<FObjUB> GObjUB;
static FUniformBuffer<FObjUB> GIdentityUB;

static FImage2DWithView GCheckerboardTexture;
static FImage2DWithView GHeightMap;
static FImage2DWithView GGradient;
static FSampler GTrilinearSampler;
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
					return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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

			auto GetStageMask = [](VkImageLayout InLayout) -> int
			{
				switch (InLayout)
				{
				case VK_IMAGE_LAYOUT_UNDEFINED:
					return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
					return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

				case VK_IMAGE_LAYOUT_GENERAL:
					return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

				case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
					return VK_PIPELINE_STAGE_TRANSFER_BIT;

				default:
					check(0);
					break;
				}

				return 0;
			};
			VkImageAspectFlagBits Aspect = IsDepthStencil(NewLayout) ? (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
			ImageBarrier(CmdBuffer, GetStageMask(Layout), GetStageMask(NewLayout), Texture.GetImage(), Layout, GetAccessMask(Layout), NewLayout, GetAccessMask(NewLayout), Aspect);
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
	FTestPSO()
		: FGfxPSO(GShaderCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, VK_SHADER_STAGE_VERTEX_BIT, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, VK_SHADER_STAGE_VERTEX_BIT, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, VK_SHADER_STAGE_FRAGMENT_BIT, 2, VK_DESCRIPTOR_TYPE_SAMPLER);
		AddBinding(OutBindings, VK_SHADER_STAGE_FRAGMENT_BIT, 3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	}
};
FTestPSO GTestPSO;

struct FGenerateMipsPSO : public FGfxPSO
{
	FGenerateMipsPSO()
		: FGfxPSO(GShaderCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, VK_SHADER_STAGE_FRAGMENT_BIT, 0, VK_DESCRIPTOR_TYPE_SAMPLER);
		AddBinding(OutBindings, VK_SHADER_STAGE_FRAGMENT_BIT, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	}
};
FGenerateMipsPSO GGenerateMipsPSO;

struct FOneImagePSO : public FComputePSO
{
	FOneImagePSO()
		: FComputePSO(GShaderCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FOneImagePSO GFillTexturePSO;

struct FTestPostComputePSO : public FComputePSO
{
	FTestPostComputePSO()
		: FComputePSO(GShaderCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	}
};
FTestPostComputePSO GTestComputePSO;
FTestPostComputePSO GTestComputePostPSO;

struct FSetupFloorPSO : public FComputePSO
{
	FSetupFloorPSO()
		: FComputePSO(GShaderCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings) override
	{
		AddBinding(OutBindings, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		AddBinding(OutBindings, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		AddBinding(OutBindings, 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		AddBinding(OutBindings, 3, VK_DESCRIPTOR_TYPE_SAMPLER);
		AddBinding(OutBindings, 4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
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
	FShaderHandle TestComputeCS = GShaderCollection.Register("../Shaders/TestComputeCS.hlsl", EShaderStage::Compute, "Main");
	FShaderHandle PassThroughVS = GShaderCollection.Register("../Shaders/PassThroughVS.hlsl", EShaderStage::Vertex, "MainVS");
	FShaderHandle TestVS = GShaderCollection.Register("../Shaders/Test.hlsl", EShaderStage::Vertex, "MainVS");
	FShaderHandle TestPS = GShaderCollection.Register("../Shaders/Test.hlsl", EShaderStage::Pixel, "MainPS");
	FShaderHandle CreateFloorCS = GShaderCollection.Register("../Shaders/CreateFloorCS.hlsl", EShaderStage::Compute, "Main");
	FShaderHandle TestPostCS = GShaderCollection.Register("../Shaders/TestPostCS.hlsl", EShaderStage::Compute, "Main");
	FShaderHandle FillTextureCS = GShaderCollection.Register("../Shaders/FillTextureCS.hlsl", EShaderStage::Compute, "Main");
	FShaderHandle GenerateMipsPS = GShaderCollection.Register("../Shaders/GenerateMipsPS.hlsl", EShaderStage::Pixel, "Main");

	GShaderCollection.ReloadShaders();

	check(GSetupFloorPSO.Create(GDevice.Device, CreateFloorCS));
	check(GTestPSO.CreateVSPS(GDevice.Device, TestVS, TestPS));
	check(GGenerateMipsPSO.CreateVSPS(GDevice.Device, PassThroughVS, GenerateMipsPS));
	check(GTestComputePostPSO.Create(GDevice.Device, TestPostCS));
	check(GFillTexturePSO.Create(GDevice.Device, FillTextureCS));
	check(GTestComputePSO.Create(GDevice.Device, TestComputeCS));

	// Setup Vertex Format
	GPosColorUVFormat.AddVertexBuffer(0, sizeof(FPosColorUVVertex), VK_VERTEX_INPUT_RATE_VERTEX);
	GPosColorUVFormat.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(FPosColorUVVertex, x));
	GPosColorUVFormat.AddVertexAttribute(0, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(FPosColorUVVertex, Color));
	GPosColorUVFormat.AddVertexAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(FPosColorUVVertex, u));

	// Load and fill geometry
//	if (!GCube.Load("../Meshes/testcube/testcube.obj"))
	if (!GCube.Load("../Meshes/cube/cube.obj"))
	{
		return false;
	}
	GCube.Create(&GDevice, &GCmdBufferMgr, &GStagingManager, &GMemMgr);

	if (!GModelName.empty())
	{
		if (!GModel.Load(GModelName.c_str()))
		{
			return false;
		}

		GModel.Create(&GDevice, &GCmdBufferMgr, &GStagingManager, &GMemMgr);
	}

	return true;
}

void GenerateMips(FCmdBuffer* CmdBuffer, FImage2DWithView& Image, std::vector<FImageView*>& OutCreatedImageViews)
{
	VkFormat Format = Image.GetFormat();
	FImageView* SourceImageView = nullptr;
	FImageView* DestImageView = nullptr;

	auto* RenderPass = GObjectCache.GetOrCreateRenderPass(Image.GetWidth(), Image.GetHeight(), 1, &Format);
	auto* Pipeline = GObjectCache.GetOrCreateGfxPipeline(&GGenerateMipsPSO, nullptr, Image.GetWidth(), Image.GetHeight(), RenderPass);
	VkViewport Viewport;
	MemZero(Viewport);
	VkRect2D Scissor;
	MemZero(Scissor);

	for (uint32 Index = 1; Index < Image.Image.NumMips; ++Index)
	{
		if (DestImageView)
		{
			SourceImageView = DestImageView;
		}
		else
		{
			SourceImageView = new FImageView();
			SourceImageView->Create(GDevice.Device, Image.Image.Image, VK_IMAGE_VIEW_TYPE_2D, Image.GetFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, Index - 1, 0);
			OutCreatedImageViews.push_back(SourceImageView);
		}
		DestImageView = new FImageView();
		DestImageView->Create(GDevice.Device, Image.Image.Image, VK_IMAGE_VIEW_TYPE_2D, Image.GetFormat(), VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, Index, 0);
		OutCreatedImageViews.push_back(DestImageView);

		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, Image.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, Index);

		auto* Framebuffer = GObjectCache.GetOrCreateFramebuffer(RenderPass->RenderPass, DestImageView->ImageView, VK_NULL_HANDLE, Image.GetWidth() >> Index, Image.GetHeight() >> Index);
		CmdBuffer->BeginRenderPass(RenderPass->RenderPass, *Framebuffer, false);
		vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Pipeline);
		Viewport.width = (float)(Image.GetWidth() >> Index);
		Viewport.height = (float)(Image.GetHeight() >> Index);
		Viewport.maxDepth = 1;
		vkCmdSetViewport(CmdBuffer->CmdBuffer, 0, 1, &Viewport);
		Scissor.extent.width = Image.GetWidth() >> Index;
		Scissor.extent.height = Image.GetHeight() >> Index;
		vkCmdSetScissor(CmdBuffer->CmdBuffer, 0, 1, &Scissor);

		auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GGenerateMipsPSO.DSLayout);
		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddSampler(DescriptorSet, 0, GTrilinearSampler);
		WriteDescriptors.AddImage(DescriptorSet, 1, GTrilinearSampler, *SourceImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		GDescriptorPool.UpdateDescriptors(WriteDescriptors);
		DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);

		vkCmdDraw(CmdBuffer->CmdBuffer, 3, 1, 0, 0);
		CmdBuffer->EndRenderPass();
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, Image.GetImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, Index);
	}
}

void CreateAndFillTexture()
{
	srand(0);
	GCheckerboardTexture.Create(GDevice.Device, 64, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);
	GHeightMap.Create(GDevice.Device, 64, 64, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);
	GGradient.Create(GDevice.Device, 256, 256, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 8);

	GCubeTest.Create(GDevice.Device, 64, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &GMemMgr, 1);

	FComputePipeline* Pipeline = GObjectCache.GetOrCreateComputePipeline(&GFillTexturePSO);

	auto* CmdBuffer = GCmdBufferMgr.AllocateCmdBuffer();
	CmdBuffer->Begin();

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, GCheckerboardTexture.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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

	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, GCheckerboardTexture.GetImage(), VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, GHeightMap.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		auto FillHeightMap = [](FPrimaryCmdBuffer* CmdBuffer, void* Data, uint32 Width, uint32 Height)
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

		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, GHeightMap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, GGradient.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
		auto FillGradient = [](FPrimaryCmdBuffer* CmdBuffer, void* Data, uint32 Width, uint32 Height)
		{
			uint32* Out = (uint32*)Data;
			uint32 OriginalHeight = Height;
			while (Height--)
			{
				uint32 InnerWidth = Width;
				while (InnerWidth--)
				{
					// From https://www.shadertoy.com/view/ls2Bz1
					//if (x < 0.25)
					//	c = vec3(0.0, 4.0 * x, 1.0);
					//else if (x < 0.5)
					//	c = vec3(0.0, 1.0, 1.0 + 4.0 * (0.25 - x));
					//else if (x < 0.75)
					//	c = vec3(4.0 * (x - 0.5), 1.0, 0.0);
					//else
					//	c = vec3(1.0, 1.0 + 4.0 * (0.75 - x), 0.0);

					float t = 1.0f - ((float)InnerWidth / (float)Width) * ((float)Height / (float)OriginalHeight);

					FVector3 R;
					if (t < 0.25)
					{
						R = FVector3(0.0f, 4.0f * t, 1.0f);
					}
					else if (t < 0.5)
					{
						R = FVector3(0.0f, 1.0f, 1.0f + 4.0f * (0.25f - t));
					}
					else if (t < 0.75)
					{
						R = FVector3(4.0f * (t - 0.5f), 1.0f, 0.0f);
					}
					else
					{
						R = FVector3(1.0f, 1.0f + 4.0f * (0.75f - t), 0.0f);
					}

					*Out++ = ToRGB8Color(R, 255);
				}
			}
		};
		auto* StagingBuffer = GStagingManager.RequestUploadBufferForImage(&GGradient.Image);
		MapAndFillImageSync(StagingBuffer, CmdBuffer, &GGradient.Image, FillGradient);
		FlushMappedBuffer(GDevice.Device, StagingBuffer);

		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, GGradient.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	// Generate Mips
	std::vector<FImageView*> ImageViews;
	GenerateMips(CmdBuffer, GGradient, ImageViews);

	CmdBuffer->End();
	GCmdBufferMgr.Submit(GDescriptorPool, CmdBuffer, GDevice.PresentQueue, nullptr, nullptr);
	CmdBuffer->WaitForFence();
	for (FImageView* View : ImageViews)
	{
		View->Destroy();
		delete View;
	}
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
		WriteDescriptors.AddSampler(DescriptorSet, 3, GTrilinearSampler);
		WriteDescriptors.AddImage(DescriptorSet, 4, GTrilinearSampler, GHeightMap.ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
		else if (!_strcmpi(Token, "-validation"))
		{
			GValidation = true;
		}
		else if (!_strnicmp(Token, "-model=", 7))
		{
			auto* Model = Token + 7;
			if (*Model == '"')
			{
				++Model;
				auto* End = strchr(Model, '"');
				check(End);
				GModelName = Model;
				GModelName = GModelName.substr(0, End - Model);
			}
			else
			{
				auto* End = strchr(Model, ' ');
				GModelName = Model;
				if (End)
				{
					GModelName = GModelName.substr(0, End - Model);
				}
			}
		}
		else if (!_strcmpi(Token, "-renderdoc"))
		{
			GRenderDoc = true;
		}
		else if (!_strcmpi(Token, "-vktrace"))
		{
			GVkTrace = true;
		}
	}

	GInstance.Create(hInstance, hWnd);
	GInstance.CreateDevice(GDevice);

	GSwapchain.Create(GInstance.Surface, GDevice.PhysicalDevice, GDevice.Device, GInstance.Surface, Width, Height);

	GCmdBufferMgr.Create(GDevice.Device, GDevice.PresentQueueFamilyIndex);

	GMemMgr.Create(GDevice.Device, GDevice.PhysicalDevice);

	GShaderCollection.Create(GDevice.Device);

	GQueryMgr.Create(&GDevice);

	GDescriptorPool.Create(GDevice.Device);
	GStagingManager.Create(GDevice.Device, &GMemMgr);

	GObjectCache.Create(&GDevice);

	if (!LoadShadersAndGeometry())
	{
		return false;
	}

	GViewUB.Create(GDevice.Device, &GMemMgr);
	//GObjUB.Create(GDevice.Device, &GMemMgr);
	GIdentityUB.Create(GDevice.Device, &GMemMgr);

	for (uint32 Index = 0; Index < NUM_CUBES; ++Index)
	{
		FMeshInstance Instance;
		Instance.ObjUB.Create(GDevice.Device, &GMemMgr);
		FMeshInstance::FObjUB& ObjUB = *Instance.ObjUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
		GCubeInstances.push_back(Instance);
	}

	{
		FObjUB& ObjUB = *GIdentityUB.GetMappedData();
		ObjUB.Obj = FMatrix4x4::GetIdentity();
	}

	GRenderTargetPool.Create(GDevice.Device, &GMemMgr);
	GTrilinearSampler.CreateTrilinear(GDevice.Device);

	CreateAndFillTexture();

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

template <typename TSetDescriptors>
static void DrawMesh(FCmdBuffer* CmdBuffer, FMesh& Mesh, TSetDescriptors SetDescriptors)
{
	for (auto* Batch : Mesh.Batches)
	{
		FImage2DWithView* Image = Batch->Image ? Batch->Image : &GGradient;
		SetDescriptors(Image);
		CmdBind(CmdBuffer, &Batch->ObjVB);
		CmdBind(CmdBuffer, &Batch->ObjIB);
		vkCmdDrawIndexed(CmdBuffer->CmdBuffer, Batch->NumIndices, 1, 0, 0, 0);
	}
}

/*
static void DrawCube(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	FObjUB& ObjUB = *GObjUB.GetMappedData();
	static float AngleDegrees = 0;
	{
		AngleDegrees += 360.0f / 10.0f / 60.0f;
		AngleDegrees = fmod(AngleDegrees, 360.0f);
	}
	ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees));

	DrawMesh(CmdBuffer, GCube,
		[&](FImage2DWithView* Image)
	{
		auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

		FWriteDescriptors WriteDescriptors;
		WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
		WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GObjUB);
		WriteDescriptors.AddSampler(DescriptorSet, 2, GTrilinearSampler);
		WriteDescriptors.AddImage(DescriptorSet, 3, GTrilinearSampler, Image->ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		GDescriptorPool.UpdateDescriptors(WriteDescriptors);

		DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);
	});
}

*/
static void DrawCubes(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	static float AngleDegrees[NUM_CUBES] = {0};
	for (int32 Index = 0; Index < (int32)GCubeInstances.size(); ++Index)
	{
		int32 Y = Index / NUM_CUBES_X;
		int32 X = Index % NUM_CUBES_X;
		auto& Instance = GCubeInstances[Index];
		FMeshInstance::FObjUB& ObjUB = *Instance.ObjUB.GetMappedData();
		//static float AngleDegrees = 0;
		{
			AngleDegrees[Index] += 360.0f / 20.0f / 60.0f + 360.0f / 10.0f / 30.0f / ((float)Index + 1);
			AngleDegrees[Index] = fmod(AngleDegrees[Index], 360.0f);
		}
		ObjUB.Obj = FMatrix4x4::GetRotationY(ToRadians(AngleDegrees[Index]));
		ObjUB.Obj.Set(3, 0, (X - NUM_CUBES_X / 2.0f) * 3);
		ObjUB.Obj.Set(3, 1, -Index * 10.0f / NUM_CUBES);
		ObjUB.Obj.Set(3, 2, (Y - NUM_CUBES_Y / 2.0f) * 3);

		DrawMesh(CmdBuffer, GCube,
			[&](FImage2DWithView* Image)
		{
			auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

			FWriteDescriptors WriteDescriptors;
			WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
			WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, Instance.ObjUB);
			WriteDescriptors.AddSampler(DescriptorSet, 2, GTrilinearSampler);
			WriteDescriptors.AddImage(DescriptorSet, 3, GTrilinearSampler, Image->ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			GDescriptorPool.UpdateDescriptors(WriteDescriptors);

			DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);
		});
	}
}

static void DrawModel(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	//FObjUB& ObjUB = *GObjUB.GetMappedData();
	FObjUB& ObjUB = *GIdentityUB.GetMappedData();
	ObjUB.Obj = FMatrix4x4::GetIdentity();

	DrawMesh(CmdBuffer, GModel,
		[&](FImage2DWithView* Image)
		{
			auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

			FWriteDescriptors WriteDescriptors;
			WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
			WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GIdentityUB);
			WriteDescriptors.AddSampler(DescriptorSet, 2, GTrilinearSampler);
			WriteDescriptors.AddImage(DescriptorSet, 3, GTrilinearSampler, Image->ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			GDescriptorPool.UpdateDescriptors(WriteDescriptors);

			DescriptorSet->Bind(CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline);
		});
	//CmdBind(CmdBuffer, &GModel.ObjVB);
	//vkCmdDraw(CmdBuffer->CmdBuffer, GModel.GetNumVertices(), 1, 0, 0);
}

static void DrawFloor(FGfxPipeline* GfxPipeline, VkDevice Device, FCmdBuffer* CmdBuffer)
{
	auto* DescriptorSet = GDescriptorPool.AllocateDescriptorSet(GTestPSO.DSLayout);

	FWriteDescriptors WriteDescriptors;
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 0, GViewUB);
	WriteDescriptors.AddUniformBuffer(DescriptorSet, 1, GIdentityUB);
	WriteDescriptors.AddSampler(DescriptorSet, 2, GTrilinearSampler);
	WriteDescriptors.AddImage(DescriptorSet, 3, GTrilinearSampler, GCheckerboardTexture.ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
	static const float RotateSpeed = 0.05f;
	static const float StepSpeed = 0.001f;
	GCamera.XRotation += (GControl.MouseMoveX * PI / 180.0f) * RotateSpeed;
	GCamera.YRotation += (GControl.MouseMoveY * PI / 180.0f) * RotateSpeed;
	//char s[256];
	//sprintf(s, "%d %d\n", GControl.MouseMoveX, GControl.MouseMoveY);
	//::OutputDebugStringA(s);
	ViewUB.View = GCamera.GetViewMatrix();
	//float Speed = 0.05f;
	GCamera.Pos = GCamera.Pos.Add(GControl.StepDirection.Mul(StepSpeed));
	//GRequestControl.StepDirection = {0, 0, 0};
	GRequestControl.MouseMoveX = 0;
	GRequestControl.MouseMoveY = 0;
	GControl.StepDirection ={0, 0, 0};
	ViewUB.View.Rows[3] = FVector4(GCamera.Pos, 1);
	ViewUB.Proj = CalculateProjectionMatrix(ToRadians(60), (float)GSwapchain.GetWidth() / (float)GSwapchain.GetHeight(), 0.1f, 1000.0f);
}


static void InternalRenderFrame(VkDevice Device, FRenderPass* RenderPass, FCmdBuffer* CmdBuffer, uint32 Width, uint32 Height)
{
	auto* GfxPipeline = GObjectCache.GetOrCreateGfxPipeline(&GTestPSO, &GPosColorUVFormat, Width, Height, RenderPass, GControl.ViewMode == EViewMode::Wireframe);
	vkCmdBindPipeline(CmdBuffer->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, GfxPipeline->Pipeline);

	SetDynamicStates(CmdBuffer->CmdBuffer, Width, Height);

	if (GModelName.empty())
	{
		DrawFloor(GfxPipeline, Device, CmdBuffer);
		//DrawCube(GfxPipeline, Device, CmdBuffer);
		DrawCubes(GfxPipeline, Device, CmdBuffer);
	}
	else
	{
		DrawModel(GfxPipeline, Device, CmdBuffer);
	}
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

	if (GControl.DoRecompileShaders)
	{
		GRequestControl.DoRecompileShaders = false;
		vkDeviceWaitIdle(GDevice.Device);
		GCmdBufferMgr.Update();
		GShaderCollection.ReloadShaders();
	}

	auto* CmdBuffer = GCmdBufferMgr.GetActivePrimaryCmdBuffer();
	CmdBuffer->Begin();
	float TimeInMS = GQueryMgr.ReadLastMSResult();
	if (TimeInMS != 0.0f)
	{
		char Text[256];
		sprintf_s(Text, "%.3f ms (%f FPS)", TimeInMS, (float)(1000.0f / TimeInMS));
		::SetWindowTextA(GInstance.Window, Text);
	}
	GQueryMgr.BeginTime(CmdBuffer);

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
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, GSwapchain.Images[GSwapchain.AcquiredImageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	{
		uint32 Width = std::min(GSwapchain.GetWidth(), SceneColor->Texture.GetWidth());
		uint32 Height = std::min(GSwapchain.GetHeight(), SceneColor->Texture.GetHeight());
		SceneColor->DoTransition(CmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		BlitColorImage(CmdBuffer, Width, Height, SceneColor->Texture.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_UNDEFINED);
		GRenderTargetPool.Release(SceneColor);
	}
	ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, GSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	GQueryMgr.EndTime(CmdBuffer);

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
	for (auto& Instance : GCubeInstances)
	{
		Instance.ObjUB.Destroy();
		//GObjUB.Destroy();
	}
	GCube.Destroy();
	GModel.Destroy();
	GIdentityUB.Destroy();

	GTrilinearSampler.Destroy();

	GCheckerboardTexture.Destroy();
	GHeightMap.Destroy();
	GGradient.Destroy();
	GCubeTest.Destroy();

	GQueryMgr.Destroy();

	GDescriptorPool.Destroy();

	GTestComputePostPSO.Destroy(GDevice.Device);
	GTestComputePSO.Destroy(GDevice.Device);
	GTestPSO.Destroy(GDevice.Device);
	GGenerateMipsPSO.Destroy(GDevice.Device);
	GSetupFloorPSO.Destroy(GDevice.Device);
	GFillTexturePSO.Destroy(GDevice.Device);

	GRenderTargetPool.Destroy();

	GSwapchain.Destroy();
	GStagingManager.Destroy();
	GObjectCache.Destroy();
	GCmdBufferMgr.Destroy();
	GShaderCollection.Destroy();
	GMemMgr.Destroy();
	GDevice.Destroy();
	GInstance.Destroy();
}
