
#pragma once

#include "VkDevice.h"
#include "VkMem.h"
#include "../Utils/Shaders.h"
#include <direct.h>

class FWriteDescriptors;
struct FVulkanShaderCollection;
class FDescriptorSet;

struct FBuffer
{
	void Create(VkDevice InDevice, uint64 InSize, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr, const char* InFile, int InLine)
	{
		Device = InDevice;
		Size = InSize;

		VkBufferCreateInfo BufferInfo;
		MemZero(BufferInfo);
		BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		BufferInfo.size = Size;
		BufferInfo.usage = UsageFlags;
		//BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		checkVk(vkCreateBuffer(Device, &BufferInfo, nullptr, &Buffer));

		vkGetBufferMemoryRequirements(Device, Buffer, &Reqs);

		bool bMapped = (MemPropertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		SubAlloc = MemMgr->Alloc(Reqs, MemPropertyFlags, false, bMapped, InFile, InLine);

		vkBindBufferMemory(Device, Buffer, SubAlloc->GetHandle(), SubAlloc->GetBindOffset());
	}

	void Destroy()
	{
		vkDestroyBuffer(Device, Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;
		Device = VK_NULL_HANDLE;

		SubAlloc->Release();
	}

	void* GetMappedData()
	{
		return SubAlloc->GetMappedData();
	}

	uint64 GetBindOffset() const
	{
		return SubAlloc->GetBindOffset();
	}

	uint64 GetSize() const
	{
		return Size;
	}

	VkDevice Device;
	VkBuffer Buffer = VK_NULL_HANDLE;
	uint64 Size = 0;
	VkMemoryRequirements Reqs;
	FMemSubAlloc* SubAlloc = nullptr;
};


struct FIndexBuffer
{
	void Create(VkDevice InDevice, uint32 InNumIndices, VkIndexType InIndexType, FMemManager* MemMgr,
		VkBufferUsageFlags InUsageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VkMemoryPropertyFlags MemPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	{
		VkBufferUsageFlags UsageFlags = InUsageFlags | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		check(InIndexType == VK_INDEX_TYPE_UINT16 || InIndexType == VK_INDEX_TYPE_UINT32);
		IndexType = InIndexType;
		NumIndices = InNumIndices;
		uint32 IndexSize = InIndexType == VK_INDEX_TYPE_UINT16 ? 2 : 4;
		Buffer.Create(InDevice, InNumIndices * IndexSize, UsageFlags, MemPropertyFlags, MemMgr, __FILE__, __LINE__);
	}

	void Destroy()
	{
		Buffer.Destroy();
	}

	FBuffer Buffer;
	uint32 NumIndices = 0;
	VkIndexType IndexType = VK_INDEX_TYPE_UINT32;
};

inline void CmdBind(FCmdBuffer* CmdBuffer, FIndexBuffer* IB)
{
	vkCmdBindIndexBuffer(CmdBuffer->CmdBuffer, IB->Buffer.Buffer, IB->Buffer.GetBindOffset(), IB->IndexType);
}

struct FVertexBuffer
{
	void Create(VkDevice InDevice, uint64 Size, FMemManager* MemMgr,
		VkBufferUsageFlags InUsageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VkMemoryPropertyFlags MemPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	{
		VkBufferUsageFlags UsageFlags = InUsageFlags | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		Buffer.Create(InDevice, Size, UsageFlags, MemPropertyFlags, MemMgr, __FILE__, __LINE__);
	}

	void Destroy()
	{
		Buffer.Destroy();
	}

	FBuffer Buffer;
};

inline void CmdBind(FCmdBuffer* CmdBuffer, FVertexBuffer* VB)
{
	VkDeviceSize Offset = 0;
	vkCmdBindVertexBuffers(CmdBuffer->CmdBuffer, 0, 1, &VB->Buffer.Buffer, &Offset);
}

template <typename TStruct>
struct FUniformBuffer
{
	void Create(VkDevice InDevice, FMemManager* MemMgr)
	{
		UploadBuffer.Create(InDevice, sizeof(TStruct), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, MemMgr, __FILE__, __LINE__);
	}

	TStruct* GetMappedData()
	{
		return (TStruct*)UploadBuffer.GetMappedData();
	}

	void Destroy()
	{
		UploadBuffer.Destroy();
	}

	FBuffer UploadBuffer;
};

template <typename TStruct>
struct FGPUUniformBuffer
{
	void Create(VkDevice InDevice, FMemManager* MemMgr)
	{
		GPUBuffer.Create(InDevice, sizeof(TStruct), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemMgr, __FILE__, __LINE__);
	}

	void Destroy()
	{
		GPUBuffer.Destroy();
	}

	FBuffer GPUBuffer;
};

struct FImage
{
	void Create2D(VkDevice InDevice, uint32 InWidth, uint32 InHeight, VkFormat InFormat, VkImageUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr, uint32 InNumMips, VkSampleCountFlagBits InSamples, bool bCubemap, uint32 NumArrayLayers, const char* InFile, int InLine)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;
		NumMips = InNumMips;
		Format = InFormat;
		Samples = InSamples;

		check(!bCubemap || Width == Height);

		VkImageCreateInfo ImageInfo;
		MemZero(ImageInfo);
		ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ImageInfo.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
		ImageInfo.imageType = VK_IMAGE_TYPE_2D;
		ImageInfo.format = Format;
		ImageInfo.extent.width = Width;
		ImageInfo.extent.height = Height;
		ImageInfo.extent.depth = 1;
		ImageInfo.mipLevels = NumMips;
		ImageInfo.arrayLayers = NumArrayLayers;
		ImageInfo.samples = Samples;
		ImageInfo.tiling = (MemPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
		ImageInfo.usage = UsageFlags;
		ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		checkVk(vkCreateImage(Device, &ImageInfo, nullptr, &Image));

		vkGetImageMemoryRequirements(Device, Image, &Reqs);

		SubAlloc = MemMgr->Alloc(Reqs, MemPropertyFlags, true, false, InFile, InLine);

		vkBindImageMemory(Device, Image, SubAlloc->GetHandle(), SubAlloc->GetBindOffset());
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyImage(Device, Image, nullptr);
		Image = VK_NULL_HANDLE;

		SubAlloc->Release();
	}

	void* GetMappedData()
	{
		return SubAlloc->GetMappedData();
	}

	uint64 GetBindOffset()
	{
		return SubAlloc->GetBindOffset();
	}

	VkDevice Device;
	VkImage Image = VK_NULL_HANDLE;
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 NumMips = 0;
	VkFormat Format = VK_FORMAT_UNDEFINED;
	VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
	VkMemoryRequirements Reqs;
	FMemSubAlloc* SubAlloc = nullptr;
};

struct FImageView
{
	VkImageView ImageView = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkFormat Format = VK_FORMAT_UNDEFINED;

	void Create(VkDevice InDevice, VkImage Image, VkImageViewType ViewType, VkFormat InFormat, VkImageAspectFlags ImageAspect, uint32 NumMips, uint32 LayerCount, uint32 StartMip = 0, uint32 StartLayer = 0)
	{
		Device = InDevice;
		Format = InFormat;

		VkImageViewCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		Info.image = Image;
		Info.viewType = ViewType;
		Info.format = Format;
		Info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		Info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		Info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		Info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		Info.subresourceRange.aspectMask = ImageAspect;
		Info.subresourceRange.baseMipLevel = StartMip;
		Info.subresourceRange.levelCount = NumMips;
		Info.subresourceRange.baseArrayLayer = StartLayer;
		Info.subresourceRange.layerCount = LayerCount;
		checkVk(vkCreateImageView(Device, &Info, nullptr, &ImageView));
	}

	void Destroy()
	{
		vkDestroyImageView(Device, ImageView, nullptr);
		ImageView = VK_NULL_HANDLE;
	}
};

struct FStagingBuffer : public FBuffer
{
	FCmdBuffer* CmdBuffer = nullptr;
	uint64 FenceCounter = 0;

	void SetFence(FPrimaryCmdBuffer* InCmdBuffer)
	{
		check(InCmdBuffer);
		CmdBuffer = InCmdBuffer;
		FenceCounter = InCmdBuffer->Fence->FenceSignaledCounter;
	}

	bool IsSignaled() const
	{
		return FenceCounter < CmdBuffer->Fence->FenceSignaledCounter;
	}
};


struct FStagingManager
{
	VkDevice Device = VK_NULL_HANDLE;
	FMemManager* MemMgr = nullptr;
	void Create(VkDevice InDevice, FMemManager* InMemMgr)
	{
		Device = InDevice;
		MemMgr = InMemMgr;
	}

	void Destroy()
	{
		Update();
		for (auto& Entry : Entries)
		{
			check(Entry.bFree);
			Entry.Buffer->Destroy();
			delete Entry.Buffer;
		}
	}

	FStagingBuffer* RequestUploadBuffer(uint64 Size)
	{
/*
		for (auto& Entry : Entries)
		{
			if (Entry.bFree && Entry.Buffer->Size == Size)
			{
				Entry.bFree = false;
				Entry.Buffer->CmdBuffer = nullptr;
				Entry.Buffer->FenceCounter = 0;
				return Entry.Buffer;
			}
		}
*/
		auto* Buffer = new FStagingBuffer;
		Buffer->Create(Device, Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, MemMgr, __FILE__, __LINE__);
		FEntry Entry;
		Entry.Buffer = Buffer;
		Entry.bFree = false;
		Entries.push_back(Entry);
		return Buffer;
	}

	FStagingBuffer* RequestUploadBufferForImage(const FImage* Image)
	{
		uint32 Size = Image->Width * Image->Height * GetFormatBitsPerPixel(Image->Format) / 8;
		return RequestUploadBuffer(Size);
	}

	void Update()
	{
		for (auto& Entry : Entries)
		{
			if (!Entry.bFree)
			{
				if (Entry.Buffer->IsSignaled())
				{
					Entry.bFree = true;
				}
			}
		}
	}

	struct FEntry
	{
		FStagingBuffer* Buffer = nullptr;
		bool bFree = false;
	};
	std::vector<FEntry> Entries;
};

inline bool IsDepthOrStencilFormat(VkFormat Format)
{
	switch (Format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_S8_UINT:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;

	default:
		return false;
	}
}

inline VkImageAspectFlags GetImageAspectFlags(VkFormat Format)
{
	switch (Format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

struct FBaseImageWithView
{
	void Destroy()
	{
		ImageView.Destroy();
		Image.Destroy(ImageView.Device);
	}

	FImage Image;
	FImageView ImageView;

	inline VkFormat GetFormat() const
	{
		return ImageView.Format;
	}

	inline VkImage GetImage() const
	{
		return Image.Image;
	}

	inline VkImageView GetImageView() const
	{
		return ImageView.ImageView;
	}
};

struct FImage2DWithView : public FBaseImageWithView
{
	void Create(VkDevice InDevice, uint32 InWidth, uint32 InHeight, VkFormat Format, VkImageUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr, uint32 InNumMips, VkSampleCountFlagBits Samples, const char* InFile, int InLine)
	{
		Image.Create2D(InDevice, InWidth, InHeight, Format, UsageFlags, MemPropertyFlags, MemMgr, InNumMips, Samples, false, 1, InFile, InLine);
		ImageView.Create(InDevice, Image.Image, VK_IMAGE_VIEW_TYPE_2D, Format, GetImageAspectFlags(Format), InNumMips, 1);
	}

	inline uint32 GetWidth() const
	{
		return Image.Width;
	}

	inline uint32 GetHeight() const
	{
		return Image.Height;
	}
};

struct FImageCubeWithView : public FBaseImageWithView
{
	void Create(VkDevice InDevice, uint32 InSize, VkFormat Format, VkImageUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr, uint32 InNumMips = 1, VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT)
	{
		Image.Create2D(InDevice, InSize, InSize, Format, UsageFlags, MemPropertyFlags, MemMgr, InNumMips, Samples, true, 6, __FILE__, __LINE__);
		ImageView.Create(InDevice, Image.Image, VK_IMAGE_VIEW_TYPE_CUBE, Format, GetImageAspectFlags(Format), InNumMips, 6);
	}

	inline uint32 GetWidth() const
	{
		return Image.Width;
	}

	inline uint32 GetHeight() const
	{
		return Image.Height;
	}
};
struct FSampler
{
	VkSampler Sampler = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void CreateTrilinear(VkDevice InDevice)
	{
		InternalCreate(InDevice, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR);
	}

	void CreatePoint(VkDevice InDevice)
	{
		InternalCreate(InDevice, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST);
	}

	void InternalCreate(VkDevice InDevice, VkFilter InMag, VkFilter InMin, VkSamplerMipmapMode InMip)
	{
		Device = InDevice;

		VkSamplerCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		//VkSamplerCreateFlags    flags;
		Info.magFilter = InMag;
		Info.minFilter = InMin;
		Info.mipmapMode = InMip;
		//VkSamplerAddressMode    addressModeU;
		//VkSamplerAddressMode    addressModeV;
		//VkSamplerAddressMode    addressModeW;
		//float                   mipLodBias;
		//VkBool32                anisotropyEnable;
		//float                   maxAnisotropy;
		//VkBool32                compareEnable;
		//VkCompareOp             compareOp;
		//float                   maxLod;
		Info.maxLod = 1.0f;
		//VkBorderColor           borderColor;
		//VkBool32                unnormalizedCoordinates;
		checkVk(vkCreateSampler(Device, &Info, nullptr, &Sampler));
	}

	void Destroy()
	{
		vkDestroySampler(Device, Sampler, nullptr);
		Sampler = VK_NULL_HANDLE;
	}
};

struct FDescriptorSetInfo
{
	uint32 DescriptorSetIndex;
	struct FBindingInfo
	{
		std::string Name;
		uint32 BindingIndex;
		enum class EType
		{
			Unknown,
			Sampler,
			SampledImage,
			StorageImage,
			CombinedSamplerImage,
			UniformBuffer,
			StorageBuffer,
		};
		EType Type = EType::Unknown;
	};
	std::map<uint32, FBindingInfo> Bindings;
};


struct FShader : public IShader
{
	VkDevice Device = VK_NULL_HANDLE;

	FShader(const FShaderInfo& InInfo)
		: IShader(InInfo)
	{
	}

	bool Create(VkDevice InDevice)
	{
		Device = InDevice;

		if (SpirV.empty())
		{
			return false;
		}

		check(SpirV.size() % 4 == 0);

		VkShaderModuleCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		CreateInfo.codeSize = SpirV.size();
		CreateInfo.pCode = (uint32*)&SpirV[0];

		checkVk(vkCreateShaderModule(Device, &CreateInfo, nullptr, &ShaderModule));

		return true;
	}

	bool Create(std::vector<char>& InSpirV, VkDevice Device)
	{
		SpirV = InSpirV;
		return Create(Device);
	}

	bool Create(const char* Filename, VkDevice Device)
	{
		SpirV = LoadFile(Filename);
		return Create(Device);
	}

	virtual void Destroy() override
	{
		if (ShaderModule != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(Device, ShaderModule, nullptr);
			ShaderModule = VK_NULL_HANDLE;
		}
	}

	void GenerateReflection(std::map<uint32, FDescriptorSetInfo>& DescriptorSets);

	std::vector<char> SpirV;
	VkShaderModule ShaderModule = VK_NULL_HANDLE;
};

struct FPSO
{
	FVulkanShaderCollection& Collection;

	FPSO(FVulkanShaderCollection& InCollection)
		: Collection(InCollection)
	{
	}

	virtual void SetupLayoutBindings(std::vector<VkDescriptorSetLayoutBinding>& OutBindings)
	{
	}

	virtual void Destroy(VkDevice Device)
	{
		if (DSLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(Device, DSLayout, nullptr);
		}
	}

	void CreateDescriptorSetLayout(VkDevice Device)
	{
		std::vector<VkDescriptorSetLayoutBinding> DSBindings;
		SetupLayoutBindings(DSBindings);
		CompareAgainstReflection(DSBindings);

		VkDescriptorSetLayoutCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		Info.bindingCount = (uint32)DSBindings.size();
		Info.pBindings = DSBindings.empty() ? nullptr : &DSBindings[0];
		checkVk(vkCreateDescriptorSetLayout(Device, &Info, nullptr, &DSLayout));

		for (auto& Sets : DescriptorSetInfo)
		{
			for (auto& Binding : Sets.second.Bindings)
			{
				auto& Entry = ReflectionInfo[Binding.second.Name];
				FPSO::FReflection Reflection;
				Reflection.DescriptorSetIndex = Sets.first;
				Reflection.BindingIndex = Binding.second.BindingIndex;
				Reflection.Type = Binding.second.Type;
				Entry.push_back(Reflection);
			}
		}
	}

	VkDescriptorSetLayout DSLayout = VK_NULL_HANDLE;

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages) const
	{
	}

	void CompareAgainstReflection(std::vector<VkDescriptorSetLayoutBinding>& Bindings);

	std::map<uint32, FDescriptorSetInfo> DescriptorSetInfo;

	struct FReflection
	{
		uint32 DescriptorSetIndex;
		uint32 BindingIndex;
		FDescriptorSetInfo::FBindingInfo::EType Type;
	};
	std::map<std::string, std::vector<FReflection>> ReflectionInfo;
};

struct FGfxPSO : public FPSO
{
	FShaderHandle VS;
	FShaderHandle PS;

	FGfxPSO(FVulkanShaderCollection& InCollection)
		: FPSO(InCollection)
	{
	}

	virtual void Destroy(VkDevice Device) override;

	bool CreateVSPS(VkDevice Device, FShaderHandle InVS, FShaderHandle InPS);

	inline void AddBinding(std::vector<VkDescriptorSetLayoutBinding>& OutBindings, VkShaderStageFlags Stage, int32 Binding, VkDescriptorType DescType, uint32 NumDescriptors = 1)
	{
		VkDescriptorSetLayoutBinding NewBinding;
		MemZero(NewBinding);
		NewBinding.binding = Binding;
		NewBinding.descriptorType = DescType;
		NewBinding.descriptorCount = NumDescriptors;
		NewBinding.stageFlags = Stage;
		OutBindings.push_back(NewBinding);
	}

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages) const override;
};

struct FVertexFormat
{
	std::vector<VkVertexInputBindingDescription> VertexBuffers;
	std::vector<VkVertexInputAttributeDescription> VertexAttributes;

	void AddVertexBuffer(uint32 Binding, uint32 Stride, VkVertexInputRate InputRate)
	{
		VkVertexInputBindingDescription VBDesc;
		MemZero(VBDesc);
		VBDesc.binding = Binding;
		VBDesc.stride = Stride;
		VBDesc.inputRate = InputRate;

		VertexBuffers.push_back(VBDesc);
	}

	void AddVertexAttribute(uint32 Binding, uint32 Location, VkFormat Format, uint32 Offset)
	{
		VkVertexInputAttributeDescription VIADesc;
		MemZero(VIADesc);
		VIADesc.binding = Binding;
		VIADesc.location = Location;
		VIADesc.format = Format;
		VIADesc.offset = Offset;
		VertexAttributes.push_back(VIADesc);
	}

	VkPipelineVertexInputStateCreateInfo GetCreateInfo() const
	{
		VkPipelineVertexInputStateCreateInfo VIInfo;
		MemZero(VIInfo);
		VIInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VIInfo.vertexBindingDescriptionCount = (uint32)VertexBuffers.size();
		VIInfo.pVertexBindingDescriptions = VertexBuffers.empty() ? nullptr : &VertexBuffers[0];
		VIInfo.vertexAttributeDescriptionCount = (uint32)VertexAttributes.size();
		VIInfo.pVertexAttributeDescriptions = VertexAttributes.empty() ? nullptr : &VertexAttributes[0];

		return VIInfo;
	}
};

struct FGfxPSOLayout
{
	FGfxPSOLayout(FGfxPSO* InGfxPSO, FVertexFormat* InVF, uint32 InWidth, uint32 InHeight, struct FRenderPass* InRenderPass, bool bInWireframe)
		: GfxPSO(InGfxPSO)
		, VF(InVF)
		, Width(InWidth)
		, Height(InHeight)
		, RenderPass(InRenderPass)
		, bWireframe(bInWireframe)
	{
	}

	friend inline bool operator < (const FGfxPSOLayout& A, const FGfxPSOLayout& B)
	{
		return memcmp(&A, &B, sizeof(A)) < 0;
	}

	FGfxPSO* GfxPSO;
	FVertexFormat* VF;
	uint32 Width;
	uint32 Height;
	struct FRenderPass* RenderPass;
	bool bWireframe;
	enum class EBlend
	{
		Opaque,
		Translucent,
	};
	EBlend Blend = EBlend::Opaque;
};

struct FComputePSO : public FPSO
{
	FComputePSO(FVulkanShaderCollection& InCollection)
		: FPSO(InCollection)
	{
	}

	FShaderHandle CS;

	virtual void Destroy(VkDevice Device) override;

	bool Create(VkDevice Device, FShaderHandle InCS);

	inline void AddBinding(std::vector<VkDescriptorSetLayoutBinding>& OutBindings, int32 Binding, VkDescriptorType DescType, uint32 NumDescriptors = 1)
	{
		VkDescriptorSetLayoutBinding NewBinding;
		MemZero(NewBinding);
		NewBinding.binding = Binding;
		NewBinding.descriptorType = DescType;
		NewBinding.descriptorCount = NumDescriptors;
		NewBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		OutBindings.push_back(NewBinding);
	}

	void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages) const override;
};

struct FBasePipeline
{
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
	const FPSO* PSO = nullptr;

	void Destroy(VkDevice Device)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;

		vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
		PipelineLayout = VK_NULL_HANDLE;
	}

	template <typename TStruct>
	bool SetUniformBuffer(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FUniformBuffer<TStruct>& UB);
	bool SetSampler(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FSampler& Sampler);
	bool SetImage(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FSampler& Sampler, const FImageView& ImageView, VkImageLayout Layout);
	bool SetStorageImage(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FImageView& ImageView);
};

class FDescriptorSet
{
public:
	FDescriptorSet(VkDescriptorSet InSet)
		: Set(InSet)
	{
	}

	void Bind(FCmdBuffer* CmdBuffer, VkPipelineBindPoint BindPoint, FBasePipeline* Pipeline)
	{
		vkCmdBindDescriptorSets(CmdBuffer->CmdBuffer, BindPoint, Pipeline->PipelineLayout, 0, 1, &Set, 0, nullptr);
		UsedFence = CmdBuffer->Fence;
		FenceCounter = CmdBuffer->Fence->FenceSignaledCounter;
	}

protected:
	VkDescriptorSet Set = VK_NULL_HANDLE;
	FFence* UsedFence = nullptr;
	uint64 FenceCounter = 0;
	friend class FWriteDescriptors;
	friend class FDescriptorPool;
};

class FDescriptorPool
{
public:
	void Create(VkDevice InDevice)
	{
		Device = InDevice;

		std::vector<VkDescriptorPoolSize> PoolSizes;
		auto AddPool = [&](VkDescriptorType Type, uint32 NumDescriptors)
		{
			VkDescriptorPoolSize PoolSize;
			MemZero(PoolSize);
			PoolSize.type = Type;
			PoolSize.descriptorCount = NumDescriptors;
			PoolSizes.push_back(PoolSize);
		};

		AddPool(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384);
		AddPool(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_SAMPLER, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16384);
		AddPool(VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 16384);

		VkDescriptorPoolCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		Info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		Info.maxSets = 32768;
		Info.poolSizeCount = (uint32)PoolSizes.size();
		Info.pPoolSizes = &PoolSizes[0];
		checkVk(vkCreateDescriptorPool(InDevice, &Info, nullptr, &Pool));
	}

	void Destroy()
	{
		vkDestroyDescriptorPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}

	FDescriptorSet* AllocateDescriptorSet(VkDescriptorSetLayout DSLayout)
	{
		auto& Entries = Sets[DSLayout];
		if (!Entries.Free.empty())
		{
			FDescriptorSet* Set = Entries.Free.back();
			Entries.Free.pop_back();
			Entries.Used.push_back(Set);
			return Set;
		}
		else
		{
			VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
			VkDescriptorSetAllocateInfo Info;
			MemZero(Info);
			Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			Info.descriptorPool = Pool;
			Info.descriptorSetCount = 1;
			Info.pSetLayouts = &DSLayout;
			checkVk(vkAllocateDescriptorSets(Device, &Info, &DescriptorSet));

			auto* NewSet = new FDescriptorSet(DescriptorSet);
			Entries.Used.push_back(NewSet);
			return NewSet;
		}
	}

	void UpdateDescriptors(FWriteDescriptors& InWriteDescriptors);
	void RefreshFences();

	VkDevice Device = VK_NULL_HANDLE;
	VkDescriptorPool Pool = VK_NULL_HANDLE;

protected:
	struct FSetsPerLayout
	{
		std::vector<FDescriptorSet*> Used;
		std::vector<FDescriptorSet*> Free;
	};
	std::map<VkDescriptorSetLayout, FSetsPerLayout> Sets;
};

struct FFramebuffer
{
	VkFramebuffer Framebuffer = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, VkRenderPass RenderPass, VkImageView ColorAttachment, VkImageView DepthAttachment, uint32 InWidth, uint32 InHeight, VkImageView ResolveColor, VkImageView ResolveDepth)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		VkImageView Attachments[4] = { ColorAttachment, DepthAttachment, VK_NULL_HANDLE, VK_NULL_HANDLE};

		uint32 NumAttachments = 1 + (DepthAttachment != VK_NULL_HANDLE ? 1 : 0);
		if (ResolveColor != VK_NULL_HANDLE)
		{
			Attachments[NumAttachments] = ResolveColor;
			++NumAttachments;
		}

		if (ResolveDepth != VK_NULL_HANDLE)
		{
			Attachments[NumAttachments] = ResolveDepth;
			++NumAttachments;
		}

		VkFramebufferCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		CreateInfo.renderPass = RenderPass;
		CreateInfo.attachmentCount = NumAttachments;
		CreateInfo.pAttachments = Attachments;
		CreateInfo.width = Width;
		CreateInfo.height = Height;
		CreateInfo.layers = 1;

		checkVk(vkCreateFramebuffer(Device, &CreateInfo, nullptr, &Framebuffer));
	}

	void Destroy()
	{
		vkDestroyFramebuffer(Device, Framebuffer, nullptr);
		Framebuffer = VK_NULL_HANDLE;
	}

	uint32 Width = 0;
	uint32 Height = 0;
};

class FRenderPassLayout
{
public:
	FRenderPassLayout() {}

	FRenderPassLayout(uint32 InWidth, uint32 InHeight, uint32 InNumColorTargets, VkFormat* InColorFormats,
		VkFormat InDepthStencilFormat = VK_FORMAT_UNDEFINED, VkSampleCountFlagBits InNumSamples = VK_SAMPLE_COUNT_1_BIT,
		VkFormat InResolveColorFormat = VK_FORMAT_UNDEFINED, VkFormat InResolveDepthFormat = VK_FORMAT_UNDEFINED)
		: Width(InWidth)
		, Height(InHeight)
		, NumColorTargets(InNumColorTargets)
		, DepthStencilFormat(InDepthStencilFormat)
		, NumSamples(InNumSamples)
		, ResolveColorFormat(InResolveColorFormat)
		, ResolveDepthFormat(InResolveDepthFormat)
	{
		Hash = Width | (Height << 16) | ((uint64)NumColorTargets << (uint64)33);
		Hash |= ((uint64)DepthStencilFormat << (uint64)56);
		Hash |= (uint64) InNumSamples << (uint64)50;

		MemZero(ColorFormats);
		uint32 ColorHash = 0;
		for (uint32 Index = 0; Index < InNumColorTargets; ++Index)
		{
			ColorFormats[Index] = InColorFormats[Index];
			ColorHash ^= (ColorFormats[Index] << (Index * 4));
		}

		Hash ^= ((uint64)ColorHash << (uint64)40);
		Hash ^= ((uint64)ResolveColorFormat << (uint64)42);
		Hash ^= ((uint64)ResolveDepthFormat << (uint64)44);
	}

	inline uint64 GetHash() const
	{
		return Hash;
	}

	inline VkSampleCountFlagBits GetNumSamples() const
	{
		return NumSamples;
	}

	enum
	{
		MAX_COLOR_ATTACHMENTS = 8
	};

protected:
	uint32 Width = 0;
	uint32 Height = 0;
	uint32 NumColorTargets = 0;
	VkFormat ColorFormats[MAX_COLOR_ATTACHMENTS];
	VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED;	// Undefined means no Depth/Stencil
	VkSampleCountFlagBits NumSamples = VK_SAMPLE_COUNT_1_BIT;
	VkFormat ResolveColorFormat = VK_FORMAT_UNDEFINED;
	VkFormat ResolveDepthFormat = VK_FORMAT_UNDEFINED;

	uint64 Hash = 0;

	friend struct FRenderPass;
};


struct FRenderPass
{
	VkRenderPass RenderPass = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, const FRenderPassLayout& InLayout);

	void Destroy()
	{
		vkDestroyRenderPass(Device, RenderPass, nullptr);
		RenderPass = VK_NULL_HANDLE;
	}

	const FRenderPassLayout& GetLayout() const
	{
		return Layout;
	}

protected:
	FRenderPassLayout Layout;
};

struct FGfxPipeline : public FBasePipeline
{
	VkPipelineInputAssemblyStateCreateInfo IAInfo;
	VkViewport Viewport;
	VkRect2D Scissor;
	VkPipelineViewportStateCreateInfo ViewportInfo;
	VkPipelineRasterizationStateCreateInfo RSInfo;
	VkPipelineMultisampleStateCreateInfo MSInfo;
	VkStencilOpState Stencil;
	VkPipelineDepthStencilStateCreateInfo DSInfo;
	VkPipelineColorBlendAttachmentState AttachState;
	VkPipelineColorBlendStateCreateInfo CBInfo;
	VkDynamicState Dynamic[2];
	VkPipelineDynamicStateCreateInfo DynamicInfo;

	FGfxPipeline();
	void Create(VkDevice Device, const FGfxPSO* InPSO, const FVertexFormat* VertexFormat, uint32 Width, uint32 Height, const FRenderPass* RenderPass);	
};

struct FComputePipeline : public FBasePipeline
{
	void Create(VkDevice Device, FComputePSO* InPSO)
	{
		PSO = InPSO;

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

class FWriteDescriptors
{
public:
	~FWriteDescriptors()
	{
		check(bClosed);
		for (auto* Info : BufferInfos)
		{
			delete Info;
		}

		for (auto* Info : ImageInfos)
		{
			delete Info;
		}
	}

	inline void AddUniformBuffer(FDescriptorSet* DescSet, uint32 Binding, const FBuffer& Buffer)
	{
		check(!bClosed);
		VkDescriptorBufferInfo* BufferInfo = new VkDescriptorBufferInfo;
		MemZero(*BufferInfo);
		BufferInfo->buffer = Buffer.Buffer;
		BufferInfo->offset = Buffer.GetBindOffset();
		BufferInfo->range = Buffer.GetSize();
		BufferInfos.push_back(BufferInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		DSWrite.pBufferInfo = BufferInfo;
		DSWrites.push_back(DSWrite);
	}

	template< typename TStruct>
	inline void AddUniformBuffer(FDescriptorSet* DescSet, uint32 Binding, const FUniformBuffer<TStruct>& Buffer)
	{
		AddUniformBuffer(DescSet, Binding, Buffer.UploadBuffer);
	}

	template< typename TStruct>
	inline void AddUniformBuffer(FDescriptorSet* DescSet, uint32 Binding, const FGPUUniformBuffer<TStruct>& Buffer)
	{
		AddUniformBuffer(DescSet, Binding, Buffer.GPUBuffer);
	}

	inline void AddStorageBuffer(FDescriptorSet* DescSet, uint32 Binding, const FBuffer& Buffer)
	{
		check(!bClosed);
		VkDescriptorBufferInfo* BufferInfo = new VkDescriptorBufferInfo;
		MemZero(*BufferInfo);
		BufferInfo->buffer = Buffer.Buffer;
		BufferInfo->offset = Buffer.GetBindOffset();
		BufferInfo->range = Buffer.GetSize();
		BufferInfos.push_back(BufferInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		DSWrite.pBufferInfo = BufferInfo;
		DSWrites.push_back(DSWrite);
	}

	inline void AddCombinedImageSampler(FDescriptorSet* DescSet, uint32 Binding, const FSampler& Sampler, const FImageView& ImageView)
	{
		check(!bClosed);
		VkDescriptorImageInfo* ImageInfo = new VkDescriptorImageInfo;
		MemZero(*ImageInfo);
		ImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		ImageInfo->imageView = ImageView.ImageView;
		ImageInfo->sampler = Sampler.Sampler;
		ImageInfos.push_back(ImageInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		DSWrite.pImageInfo = ImageInfo;
		DSWrites.push_back(DSWrite);
	}

	inline void AddImage(FDescriptorSet* DescSet, uint32 Binding, const FSampler& Sampler, const FImageView& ImageView, VkImageLayout Layout)
	{
		check(!bClosed);
		VkDescriptorImageInfo* ImageInfo = new VkDescriptorImageInfo;
		MemZero(*ImageInfo);
		ImageInfo->imageLayout = Layout;
		ImageInfo->imageView = ImageView.ImageView;
		ImageInfo->sampler = Sampler.Sampler;
		ImageInfos.push_back(ImageInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		DSWrite.pImageInfo = ImageInfo;
		DSWrites.push_back(DSWrite);
	}

	inline void AddSampler(FDescriptorSet* DescSet, uint32 Binding, const FSampler& Sampler)
	{
		check(!bClosed);
		VkDescriptorImageInfo* ImageInfo = new VkDescriptorImageInfo;
		MemZero(*ImageInfo);
		ImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		ImageInfo->imageView = VK_NULL_HANDLE;
		ImageInfo->sampler = Sampler.Sampler;
		ImageInfos.push_back(ImageInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		DSWrite.pImageInfo = ImageInfo;
		DSWrites.push_back(DSWrite);
	}

	inline void AddStorageImage(FDescriptorSet* DescSet, uint32 Binding, const FImageView& ImageView)
	{
		check(!bClosed);
		VkDescriptorImageInfo* ImageInfo = new VkDescriptorImageInfo;
		MemZero(*ImageInfo);
		ImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		ImageInfo->imageView = ImageView.ImageView;
		ImageInfos.push_back(ImageInfo);

		VkWriteDescriptorSet DSWrite;
		MemZero(DSWrite);
		DSWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DSWrite.dstSet = DescSet->Set;
		DSWrite.dstBinding = Binding;
		DSWrite.descriptorCount = 1;
		DSWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		DSWrite.pImageInfo = ImageInfo;
		DSWrites.push_back(DSWrite);
	}

protected:
	std::vector<VkDescriptorBufferInfo*> BufferInfos;
	std::vector<VkDescriptorImageInfo*> ImageInfos;
	std::vector<VkWriteDescriptorSet> DSWrites;
	bool bClosed = false;

	friend class FDescriptorPool;
};


inline void ImageBarrier(FCmdBuffer* CmdBuffer, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DestStage, VkImage Image, VkImageLayout SrcLayout, VkAccessFlags SrcMask, VkImageLayout DestLayout, VkAccessFlags DstMask, VkImageAspectFlags AspectMask, uint32 NumMips = 1, uint32 StartMip = 0)
{
	VkImageMemoryBarrier Barrier;
	MemZero(Barrier);
	Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	Barrier.srcAccessMask = SrcMask;
	Barrier.dstAccessMask = DstMask;
	Barrier.oldLayout = SrcLayout;
	Barrier.newLayout = DestLayout;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Image;
	Barrier.subresourceRange.aspectMask = AspectMask;;
	Barrier.subresourceRange.layerCount = 1;
	Barrier.subresourceRange.baseMipLevel = StartMip;
	Barrier.subresourceRange.levelCount = NumMips;
	vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, SrcStage, DestStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
}

inline void BufferBarrier(FCmdBuffer* CmdBuffer, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DestStage, VkBuffer Buffer, VkDeviceSize Offset, VkDeviceSize Size, VkAccessFlags SrcMask, VkAccessFlags DstMask)
{
	VkBufferMemoryBarrier Barrier;
	MemZero(Barrier);
	Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	Barrier.srcAccessMask = SrcMask;
	Barrier.dstAccessMask = DstMask;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.buffer = Buffer;
	Barrier.offset = Offset;
	Barrier.size = Size;
	vkCmdPipelineBarrier(CmdBuffer->CmdBuffer, SrcStage, DestStage, 0, 0, nullptr, 1, &Barrier, 0, nullptr);
}

inline void BufferBarrier(FCmdBuffer* CmdBuffer, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DestStage, FBuffer* Buffer, VkAccessFlags SrcMask, VkAccessFlags DstMask)
{
	BufferBarrier(CmdBuffer, SrcStage, DestStage, Buffer->Buffer, Buffer->GetBindOffset(), Buffer->GetSize(), SrcMask, DstMask);
}

struct FSwapchain
{
	void Create(VkSurfaceKHR SurfaceKHR, VkPhysicalDevice PhysicalDevice, VkDevice InDevice, VkSurfaceKHR Surface, uint32& WindowWidth, uint32& WindowHeight);

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> Images;
	std::vector<FImageView> ImageViews;
	VkDevice Device = VK_NULL_HANDLE;
	std::vector<FSemaphore> PresentCompleteSemaphores;
	std::vector<FSemaphore> RenderingSemaphores;
	uint32 PresentCompleteSemaphoreIndex = 0;
	uint32 RenderingSemaphoreIndex = 0;
	VkExtent2D SurfaceResolution;
	VkFormat Format = VK_FORMAT_UNDEFINED;

	uint32 AcquiredImageIndex = UINT32_MAX;

	inline VkImage GetAcquiredImage()
	{
		return Images[AcquiredImageIndex];
	}

	inline VkImageView GetAcquiredImageView()
	{
		return ImageViews[AcquiredImageIndex].ImageView;
	}

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

	inline uint32 GetWidth() const
	{
		return SurfaceResolution.width;
	}


	inline uint32 GetHeight() const
	{
		return SurfaceResolution.height;
	}

	void ClearAndTransitionToPresent(FPrimaryCmdBuffer* CmdBuffer)
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
			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, Images[Index], VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
			vkCmdClearColorImage(CmdBuffer->CmdBuffer, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Color, 1, &Range);
			ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, Images[Index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_MEMORY_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
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


inline void FlushMappedBuffer(VkDevice Device, FBuffer* Buffer)
{
	if (!Buffer->SubAlloc->IsCoherent())
	{
		VkMappedMemoryRange Range;
		MemZero(Range);
		Range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		Range.offset = Buffer->GetBindOffset();
		Range.memory = Buffer->SubAlloc->GetHandle();
		Range.size = Buffer->GetSize();
		vkInvalidateMappedMemoryRanges(Device, 1, &Range);
	}
}

inline void CopyBuffer(FPrimaryCmdBuffer* CmdBuffer, FBuffer* SrcBuffer, FBuffer* DestBuffer)
{
	VkBufferCopy Region;
	MemZero(Region);
	Region.srcOffset = SrcBuffer->GetBindOffset();
	Region.size = SrcBuffer->GetSize();
	Region.dstOffset = DestBuffer->GetBindOffset();
	vkCmdCopyBuffer(CmdBuffer->CmdBuffer, SrcBuffer->Buffer, DestBuffer->Buffer, 1, &Region);
}

template <typename TFillLambda>
inline void MapAndFillBufferSync(FStagingBuffer* StagingBuffer, FPrimaryCmdBuffer* CmdBuffer, FBuffer* DestBuffer, TFillLambda Fill, uint32 Size, void* UserData)
{
	void* BufferData = StagingBuffer->GetMappedData();
	check(BufferData);
	Fill(BufferData, UserData);

	CopyBuffer(CmdBuffer, StagingBuffer, DestBuffer);
	StagingBuffer->SetFence(CmdBuffer);
}

struct FOneShotCmdBuffer
{
	FPrimaryCmdBuffer* CmdBuffer;
	FCmdBufferMgr* CmdBufferMgr;
	FDevice* Device;
	FOneShotCmdBuffer(FDevice* InDevice, FCmdBufferMgr* InCmdBufferMgr)
		: CmdBufferMgr(InCmdBufferMgr)
		, Device(InDevice)
	{
		CmdBuffer = CmdBufferMgr->AllocateCmdBuffer();
		CmdBuffer->Begin();
	}

	~FOneShotCmdBuffer()
	{
		CmdBuffer->End();
		CmdBufferMgr->Submit(CmdBuffer, Device->PresentQueue, {}, nullptr);
		CmdBuffer->WaitForFence();
	}
};

template <typename TFillLambda>
void MapAndFillBufferSyncOneShotCmdBuffer(FDevice* Device, FCmdBufferMgr* CmdBufferMgr, FStagingManager* StagingMgr, FBuffer* DestBuffer, TFillLambda Fill, uint32 Size, void* UserData)
{
	FOneShotCmdBuffer OneShotCmdBuffer(Device, CmdBufferMgr);
	auto* CmdBuffer = OneShotCmdBuffer.CmdBuffer;
	FStagingBuffer* StagingBuffer = StagingMgr->RequestUploadBuffer(Size);
	MapAndFillBufferSync(StagingBuffer, CmdBuffer, DestBuffer, Fill, Size, UserData);
	FlushMappedBuffer(Device->Device, StagingBuffer);
}

template <typename TFillLambda>
inline void MapAndFillImageSync(FStagingBuffer* StagingBuffer, FPrimaryCmdBuffer* CmdBuffer, FImage* DestImage, TFillLambda Fill)
{
	void* Data = StagingBuffer->GetMappedData();
	check(Data);
	Fill(CmdBuffer, Data, DestImage->Width, DestImage->Height);

	{
		VkBufferImageCopy Region;
		MemZero(Region);
		Region.bufferOffset = StagingBuffer->GetBindOffset();
		Region.bufferRowLength = DestImage->Width;
		Region.bufferImageHeight = DestImage->Height;
		Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.imageSubresource.layerCount = 1;
		Region.imageExtent.width = DestImage->Width;
		Region.imageExtent.height = DestImage->Height;
		Region.imageExtent.depth = 1;
		vkCmdCopyBufferToImage(CmdBuffer->CmdBuffer, StagingBuffer->Buffer, DestImage->Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
	}

	StagingBuffer->SetFence(CmdBuffer);
}

template <typename TFillLambda>
void MapAndFillImageSyncOneShotCmdBuffer(FDevice* Device, FCmdBufferMgr* CmdBufferMgr, FStagingManager* StagingMgr, FImage* DestImage, TFillLambda Fill, uint32 Size)
{
	FOneShotCmdBuffer OneShotCmdBuffer(Device, CmdBufferMgr);
	auto* CmdBuffer = OneShotCmdBuffer.CmdBuffer;
	FStagingBuffer* StagingBuffer = StagingMgr->RequestUploadBuffer(Size);
	MapAndFillImageSync(StagingBuffer, CmdBuffer, DestImage, Fill);
	FlushMappedBuffer(Device->Device, StagingBuffer);
}

inline void CopyColorImage(FPrimaryCmdBuffer* CmdBuffer, uint32 Width, uint32 Height, VkImage SrcImage, VkImageLayout SrcCurrentLayout, VkImage DstImage, VkImageLayout DstCurrentLayout)
{
	check(CmdBuffer->State == FPrimaryCmdBuffer::EState::Begun);
	VkImageCopy CopyRegion;
	MemZero(CopyRegion);
	CopyRegion.extent.width = Width;
	CopyRegion.extent.height = Height;
	CopyRegion.extent.depth = 1;
	CopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	CopyRegion.srcSubresource.layerCount = 1;
	CopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	CopyRegion.dstSubresource.layerCount = 1;
	if (SrcCurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, SrcImage, SrcCurrentLayout, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	if (DstCurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, DstImage, DstCurrentLayout, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	vkCmdCopyImage(CmdBuffer->CmdBuffer, SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &CopyRegion);
};

inline void BlitColorImage(FPrimaryCmdBuffer* CmdBuffer, uint32 Width, uint32 Height, VkImage SrcImage, VkImageLayout SrcCurrentLayout, VkImage DstImage, VkImageLayout DstCurrentLayout)
{
	check(CmdBuffer->State == FPrimaryCmdBuffer::EState::Begun);
	VkImageBlit BlitRegion;
	MemZero(BlitRegion);
	BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	BlitRegion.srcOffsets[1].x = Width;
	BlitRegion.srcOffsets[1].y = Height;
	BlitRegion.srcOffsets[1].z = 1;
	BlitRegion.srcSubresource.layerCount = 1;
	BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	BlitRegion.dstOffsets[1].x = Width;
	BlitRegion.dstOffsets[1].y = Height;
	BlitRegion.dstOffsets[1].z = 1;
	BlitRegion.dstSubresource.layerCount = 1;
	if (SrcCurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, SrcImage, SrcCurrentLayout, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	if (DstCurrentLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		ImageBarrier(CmdBuffer, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, DstImage, DstCurrentLayout, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}
	vkCmdBlitImage(CmdBuffer->CmdBuffer, SrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, DstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &BlitRegion, VK_FILTER_NEAREST);
};


struct FVulkanShaderCollection : FShaderCollection
{
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;
	}

	void DestroyShader(FShaderHandle Handle)
	{
		IShader* Shader = GetVulkanShader(Handle);
		if (Shader)
		{
			//#todo Sync GPU
			Shader->Destroy();
			delete Shader;
			ShaderInfos[Handle.ID].Shader = nullptr;
		}
	}

	template <typename TPSOType>
	void RegisterGfxPSO(const char* Name, FShaderHandle VertexHandle, FShaderHandle PixelHandle)
	{
		TPSOType* Pipeline = new TPSOType;
		Pipeline->CreateVSPS(Device, VertexHandle, PixelHandle);
		FShaderCollection::RegisterGfxPSO(Name, Pipeline, GetVulkanShader(VertexHandle), GetVulkanShader(PixelHandle));
	}

	virtual void DestroyAndDelete(FBasePipeline* PSO) override
	{
		PSO->Destroy(Device);
		delete PSO;
	}

	void Destroy()
	{
		for (auto& Info : ShaderInfos)
		{
			if (Info.Shader)
			{
				Info.Shader->Destroy();
				delete Info.Shader;
			}
		}
		ShaderInfos.clear();
	}

	virtual bool DoCompileFromSource(FShaderInfo& Info) override
	{
		static const std::string GlslangProlog = GetGlslangCommandLine();

		std::string Compile = GlslangProlog;
		Compile += " -e " + Info.Entry;
		Compile += " -o " + FileUtils::AddQuotes(Info.BinaryFile);
		Compile += " -S " + GetStageName(Info.Stage);
		Compile += " " + FileUtils::AddQuotes(Info.SourceFile);
		Compile += " > " + FileUtils::AddQuotes(Info.AsmFile);
		int ReturnCode = system(Compile.c_str());
		if (ReturnCode)
		{
			std::vector<char> File = LoadFile(Info.AsmFile.c_str());
			if (File.empty())
			{
				std::string Error = "Compile error: No output for file ";
				Error += Info.SourceFile;
				::OutputDebugStringA(Error.c_str());
			}
			else
			{
				std::string FileString = &File[0];
				FileString.resize(File.size());
				std::string Error = "Compile error:\n";
				Error += FileString;
				Error += "\n";
				::OutputDebugStringA(Error.c_str());

				int DialogResult = ::MessageBoxA(nullptr, Error.c_str(), Info.SourceFile.c_str(), MB_CANCELTRYCONTINUE);
				if (DialogResult == IDTRYAGAIN)
				{
					return DoCompileFromSource(Info);
				}				
			}

			return false;
		}

		return DoCompileFromBinary(Info);
	}

	virtual bool DoCompileFromBinary(FShaderInfo& Info) override
	{
		std::vector<char> File = LoadFile(Info.BinaryFile.c_str());
		if (File.empty())
		{
			check(0);
			return false;
		}

		//#todo: Destroy old; sync with rendering
		if (Info.Shader)
		{
			ShadersToDestroy.push_back(Info.Shader);
		}
		Info.Shader = CreateShader(Info, File);

		return Info.Shader != nullptr;
	}

	virtual void SetupFilenames(const std::string& OriginalFilename, FShaderInfo& Info) override
	{
		std::string RootDir;
		std::string BaseFilename;
		std::string Extension = FileUtils::SplitPath(OriginalFilename, RootDir, BaseFilename, false);

		std::string OutDir = FileUtils::MakePath(RootDir, "out");
		_mkdir(OutDir.c_str());

		Info.SourceFile = FileUtils::MakePath(RootDir, BaseFilename + "." + Extension);
		Info.BinaryFile = FileUtils::MakePath(OutDir, BaseFilename + "." + Info.Entry + ".spv");
		Info.AsmFile = FileUtils::MakePath(OutDir, BaseFilename + "." + Info.Entry + ".spvasm");
	}

	static std::string GetGlslangCommandLine()
	{
		std::string Out;
		char Glslang[MAX_PATH];
		char SDKDir[MAX_PATH];
		::GetEnvironmentVariableA("VULKAN_SDK", SDKDir, MAX_PATH - 1);
		sprintf_s(Glslang, "%s\\Bin\\glslangValidator.exe", SDKDir);
		Out = Glslang;
		Out += " -V -r -l -H -D --hlsl-iomap --auto-map-bindings";
		return Out;
	}

	static std::string GetStageName(EShaderStage Stage)
	{
		switch (Stage)
		{
		case EShaderStage::Compute: return "comp";
		case EShaderStage::Vertex:	return "vert";
		case EShaderStage::Pixel:	return "frag";
		default:
			break;
		}

		return "INVALID";
	}

	virtual IShader* CreateShader(FShaderInfo& Info, std::vector<char>& Data) override
	{
		FShader* Shader = new FShader(Info);
		Shader->Stage = Info.Stage;
		if (Shader->Create(Data, Device))
		{
			return Shader;
		}

		delete Shader;
		return nullptr;
	}

	VkShaderModule GetShaderModule(FShaderHandle Handle)
	{
		IShader* Shader = GetShader(Handle);
		if (Shader)
		{
			return ((FShader*)Shader)->ShaderModule;
		}

		check(0);
		return VK_NULL_HANDLE;
	}

	FShader* GetVulkanShader(FShaderHandle Handle)
	{
		IShader* Shader = GetShader(Handle);
		if (Shader)
		{
			return (FShader*)Shader;
		}

		return nullptr;
	}
};


template <typename TStruct>
inline bool FBasePipeline::SetUniformBuffer(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FUniformBuffer<TStruct>& UB)
{
	auto Found = PSO->ReflectionInfo.find(Name);
	if (Found != PSO->ReflectionInfo.end())
	{
		for (const FPSO::FReflection& Reflection : Found->second)
		{
			check(Reflection.Type == FDescriptorSetInfo::FBindingInfo::EType::UniformBuffer);
			WriteDescriptors.AddUniformBuffer(DescriptorSet, Reflection.BindingIndex, UB);
		}

		return true;
	}
	return false;
}

inline bool FBasePipeline::SetSampler(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FSampler& Sampler)
{
	auto Found = PSO->ReflectionInfo.find(Name);
	if (Found != PSO->ReflectionInfo.end())
	{
		for (const FPSO::FReflection& Reflection : Found->second)
		{
			check(Reflection.Type == FDescriptorSetInfo::FBindingInfo::EType::Sampler);
			WriteDescriptors.AddSampler(DescriptorSet, Reflection.BindingIndex, Sampler);
		}

		return true;
	}
	return false;
}

inline bool FBasePipeline::SetImage(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FSampler& Sampler, const FImageView& ImageView, VkImageLayout Layout)
{
	auto Found = PSO->ReflectionInfo.find(Name);
	if (Found != PSO->ReflectionInfo.end())
	{
		for (const FPSO::FReflection& Reflection : Found->second)
		{
			check(Reflection.Type == FDescriptorSetInfo::FBindingInfo::EType::SampledImage);
			WriteDescriptors.AddImage(DescriptorSet, Reflection.BindingIndex, Sampler, ImageView, Layout);
		}

		return true;
	}
	return false;
}

inline bool FBasePipeline::SetStorageImage(FWriteDescriptors& WriteDescriptors, FDescriptorSet* DescriptorSet, const char* Name, const FImageView& ImageView)
{
	auto Found = PSO->ReflectionInfo.find(Name);
	if (Found != PSO->ReflectionInfo.end())
	{
		for (const FPSO::FReflection& Reflection : Found->second)
		{
			check(Reflection.Type == FDescriptorSetInfo::FBindingInfo::EType::StorageImage);
			WriteDescriptors.AddStorageImage(DescriptorSet, Reflection.BindingIndex, ImageView);
		}

		return true;
	}
	return false;
}
