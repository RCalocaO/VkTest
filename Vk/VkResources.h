
#pragma once

#include "Vk.h"
#include "VkMem.h"

struct FBuffer : public FRecyclableResource
{
	void Create(VkDevice InDevice, uint64 InSize, VkBufferUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr)
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

		SubAlloc = MemMgr->Alloc(Reqs, MemPropertyFlags, false);

		vkBindBufferMemory(Device, Buffer, SubAlloc->GetHandle(), SubAlloc->GetBindOffset());
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyBuffer(Device, Buffer, nullptr);
		Buffer = VK_NULL_HANDLE;

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


struct FImage : public FRecyclableResource
{
	void Create(VkDevice InDevice, uint32 InWidth, uint32 InHeight, VkFormat Format, VkImageUsageFlags UsageFlags, VkMemoryPropertyFlags MemPropertyFlags, FMemManager* MemMgr)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		VkImageCreateInfo ImageInfo;
		MemZero(ImageInfo);
		ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ImageInfo.imageType = VK_IMAGE_TYPE_2D;
		ImageInfo.format = Format;
		ImageInfo.extent.width = Width;
		ImageInfo.extent.height = Height;
		ImageInfo.extent.depth = 1;
		ImageInfo.mipLevels = 1;
		ImageInfo.arrayLayers = 1;
		ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		ImageInfo.usage = UsageFlags;
		ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		checkVk(vkCreateImage(Device, &ImageInfo, nullptr, &Image));

		vkGetImageMemoryRequirements(Device, Image, &Reqs);

		SubAlloc = MemMgr->Alloc(Reqs, MemPropertyFlags, true);

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
	VkMemoryRequirements Reqs;
	FMemSubAlloc* SubAlloc = nullptr;
};


struct FImageView
{
	VkImageView ImageView = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice, VkImage Image, VkImageViewType ViewType, VkFormat Format, VkImageAspectFlags ImageAspect)
	{
		Device = InDevice;

		VkImageViewCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		Info.image = Image;
		Info.viewType = ViewType;
		Info.format = Format;
		Info.components.r = VK_COMPONENT_SWIZZLE_R;
		Info.components.g = VK_COMPONENT_SWIZZLE_G;
		Info.components.b = VK_COMPONENT_SWIZZLE_B;
		Info.components.a = VK_COMPONENT_SWIZZLE_A;
		Info.subresourceRange.aspectMask = ImageAspect;
		Info.subresourceRange.levelCount = 1;
		Info.subresourceRange.layerCount = 1;
		checkVk(vkCreateImageView(Device, &Info, nullptr, &ImageView));
	}

	void Destroy()
	{
		vkDestroyImageView(Device, ImageView, nullptr);
		ImageView = VK_NULL_HANDLE;
	}
};

struct FSampler
{
	VkSampler Sampler = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;

		VkSamplerCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		//VkSamplerCreateFlags    flags;
		Info.magFilter = VK_FILTER_LINEAR;
		Info.minFilter = VK_FILTER_LINEAR;
		Info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		//VkSamplerAddressMode    addressModeU;
		//VkSamplerAddressMode    addressModeV;
		//VkSamplerAddressMode    addressModeW;
		//float                   mipLodBias;
		//VkBool32                anisotropyEnable;
		//float                   maxAnisotropy;
		//VkBool32                compareEnable;
		//VkCompareOp             compareOp;
		//float                   minLod;
		//float                   maxLod;
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

struct FShader : public FRecyclableResource
{
	bool Create(const char* Filename, VkDevice Device)
	{
		SpirV = LoadFile(Filename);
		if (SpirV.empty())
		{
			return false;
		}

		VkShaderModuleCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		check(SpirV.size() % 4 == 0);
		CreateInfo.codeSize = SpirV.size();
		CreateInfo.pCode = (uint32*)&SpirV[0];

		checkVk(vkCreateShaderModule(Device, &CreateInfo, nullptr, &ShaderModule));
		return true;
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyShaderModule(Device, ShaderModule, nullptr);
		ShaderModule = VK_NULL_HANDLE;
	}

	std::vector<char> SpirV;
	VkShaderModule ShaderModule = VK_NULL_HANDLE;
};


struct FPSO
{
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

		VkDescriptorSetLayoutCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		Info.bindingCount = DSBindings.size();
		Info.pBindings = DSBindings.empty() ? nullptr : &DSBindings[0];
		checkVk(vkCreateDescriptorSetLayout(Device, &Info, nullptr, &DSLayout));
	}

	VkDescriptorSetLayout DSLayout = VK_NULL_HANDLE;

	virtual void SetupShaderStages(std::vector<VkPipelineShaderStageCreateInfo>& OutShaderStages)
	{
	}
};


struct FGfxPSO : public FPSO
{
	FShader VS;
	FShader PS;

	virtual void Destroy(VkDevice Device) override
	{
		FPSO::Destroy(Device);
		PS.Destroy(Device);
		VS.Destroy(Device);
	}

	bool CreateVSPS(VkDevice Device, const char* VSFilename, const char* PSFilename)
	{
		if (!VS.Create(VSFilename, Device))
		{
			return false;
		}

		if (!PS.Create(PSFilename, Device))
		{
			return false;
		}

		CreateDescriptorSetLayout(Device);
		return true;
	}
};

struct FComputePSO : public FPSO
{
	FShader CS;

	virtual void Destroy(VkDevice Device) override
	{
		FPSO::Destroy(Device);
		CS.Destroy(Device);
	}

	bool Create(VkDevice Device, const char* CSFilename)
	{
		if (!CS.Create(CSFilename, Device))
		{
			return false;
		}

		CreateDescriptorSetLayout(Device);
		return true;
	}
};

struct FBasePipeline
{
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;

	void Destroy(VkDevice Device)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;

		vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
		PipelineLayout = VK_NULL_HANDLE;
	}
};

struct FDescriptorPool
{
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
		AddPool(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_SAMPLER, 32768);
		AddPool(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32768);

		VkDescriptorPoolCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		Info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		Info.maxSets = 32768;
		Info.poolSizeCount = PoolSizes.size();
		Info.pPoolSizes = &PoolSizes[0];
		checkVk(vkCreateDescriptorPool(InDevice, &Info, nullptr, &Pool));
	}

	void Destroy()
	{
		vkDestroyDescriptorPool(Device, Pool, nullptr);
		Pool = VK_NULL_HANDLE;
	}

	VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout DSLayout)
	{
		VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSetAllocateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		Info.descriptorPool = Pool;
		Info.descriptorSetCount = 1;
		Info.pSetLayouts = &DSLayout;
		checkVk(vkAllocateDescriptorSets(Device, &Info, &DescriptorSet));
		return DescriptorSet;
	}

	VkDevice Device = VK_NULL_HANDLE;
	VkDescriptorPool Pool = VK_NULL_HANDLE;
};

struct FFence : public FRecyclableResource
{
	VkFence Fence = VK_NULL_HANDLE;
	uint64 FenceSignaledCounter = 0;
	VkDevice Device = VK_NULL_HANDLE;

	enum EState
	{
		NotSignaled,
		Signaled,
	};
	EState State = EState::NotSignaled;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;

		VkFenceCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		checkVk(vkCreateFence(Device, &Info, nullptr, &Fence));
	}

	void Destroy(VkDevice Device)
	{
		vkDestroyFence(Device, Fence, nullptr);
		Fence = VK_NULL_HANDLE;
	}

	void Wait(uint64 TimeInNanoseconds = 0xffffffff)
	{
		check(State == EState::NotSignaled);
		checkVk(vkWaitForFences(Device, 1, &Fence, true, TimeInNanoseconds));
		RefreshState();
	}

	bool IsNotSignaled() const
	{
		return State == EState::NotSignaled;
	}

	void RefreshState()
	{
		if (State == EState::NotSignaled)
		{
			VkResult Result = vkGetFenceStatus(Device, Fence);
			switch (Result)
			{
			case VK_SUCCESS:
				++FenceSignaledCounter;
				State = EState::Signaled;
				checkVk(vkResetFences(Device, 1, &Fence));
				break;
			case VK_NOT_READY:
				break;
			default:
				checkVk(Result);
				break;
			}
		}
	}
};

struct FSemaphore : public FRecyclableResource
{
	VkSemaphore Semaphore = VK_NULL_HANDLE;

	void Create(VkDevice Device)
	{
		VkSemaphoreCreateInfo Info;
		MemZero(Info);
		Info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		checkVk(vkCreateSemaphore(Device, &Info, nullptr, &Semaphore));
	}

	void Destroy(VkDevice Device)
	{
		vkDestroySemaphore(Device, Semaphore, nullptr);
		Semaphore = VK_NULL_HANDLE;
	}
};

struct FFramebuffer : public FRecyclableResource
{
	VkFramebuffer Framebuffer = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void CreateColorOnly(VkDevice InDevice, VkRenderPass RenderPass, VkImageView ColorAttachment, uint32 InWidth, uint32 InHeight)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		VkFramebufferCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		CreateInfo.renderPass = RenderPass;
		CreateInfo.attachmentCount = 1;
		CreateInfo.pAttachments = &ColorAttachment;
		CreateInfo.width = Width;
		CreateInfo.height = Height;
		CreateInfo.layers = 1;

		checkVk(vkCreateFramebuffer(Device, &CreateInfo, nullptr, &Framebuffer));
	}

	void CreateColorAndDepth(VkDevice InDevice, VkRenderPass RenderPass, VkImageView ColorAttachment, VkImageView DepthAttachment, uint32 InWidth, uint32 InHeight)
	{
		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		VkImageView Attachments[2] = {ColorAttachment, DepthAttachment};

		VkFramebufferCreateInfo CreateInfo;
		MemZero(CreateInfo);
		CreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		CreateInfo.renderPass = RenderPass;
		CreateInfo.attachmentCount = 2;
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


struct FRenderPass : public FRecyclableResource
{
	VkRenderPass RenderPass = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;

	void Create(VkDevice InDevice)
	{
		Device = InDevice;

		VkAttachmentDescription AttachmentDesc[2];
		MemZero(AttachmentDesc);
		AttachmentDesc[0].format = VK_FORMAT_R8G8B8A8_UNORM;
		AttachmentDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
		AttachmentDesc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		AttachmentDesc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		AttachmentDesc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		AttachmentDesc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		AttachmentDesc[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		AttachmentDesc[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		AttachmentDesc[1].format = VK_FORMAT_D32_SFLOAT;
		AttachmentDesc[1].samples = VK_SAMPLE_COUNT_1_BIT;
		AttachmentDesc[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		AttachmentDesc[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		AttachmentDesc[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		AttachmentDesc[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		AttachmentDesc[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		AttachmentDesc[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference AttachmentRef[2];
		MemZero(AttachmentRef);
		AttachmentRef[0].attachment = 0;
		AttachmentRef[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		AttachmentRef[1].attachment = 1;
		AttachmentRef[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription Subpass;
		MemZero(Subpass);
		Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		Subpass.colorAttachmentCount = 1;
		Subpass.pColorAttachments = &AttachmentRef[0];
		Subpass.pDepthStencilAttachment = &AttachmentRef[1];

		VkRenderPassCreateInfo RenderPassInfo;
		MemZero(RenderPassInfo);
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		RenderPassInfo.attachmentCount = sizeof(AttachmentDesc) / sizeof(AttachmentDesc[0]);
		RenderPassInfo.pAttachments = AttachmentDesc;
		RenderPassInfo.subpassCount = 1;
		RenderPassInfo.pSubpasses = &Subpass;

		checkVk(vkCreateRenderPass(Device, &RenderPassInfo, nullptr, &RenderPass));
	}

	void Destroy()
	{
		vkDestroyRenderPass(Device, RenderPass, nullptr);
		RenderPass = VK_NULL_HANDLE;
	}
};

