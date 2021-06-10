
#include "vk_renderer.h"

#define FREEZE_SHADOWS 0

namespace vk::renderer
{
  global_variable vulkan_platform_state g_PlatformState {};
  global_variable vulkan_render_state g_RenderState {};

  static VkAllocationCallbacks* g_VkAllocator = NULL;
  global_variable VmaAllocator g_Allocator;

  allocated_buffer_untyped
  CreateBuffer(u64 allocSize,
               VkBufferUsageFlags usage,
               VmaMemoryUsage memoryUsage,
               VkMemoryPropertyFlags requiredFlags = 0);

  void
  ReallocateBuffer(allocated_buffer_untyped& buffer,
                   size_t allocSize,
                   VkBufferUsageFlags usage,
                   VmaMemoryUsage memoryUsage,
                   VkMemoryPropertyFlags required_flags = 0);

  void
  ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

  std::string AssetPath(std::string_view path)
  {
	  return "data/assets_export/" + std::string(path);
  }

  std::string ShaderPath(std::string_view path) 
  {
  	return "data/shaders/" + std::string(path);
  }

  // void VulkanEngine::refresh_renderbounds(MeshObject *object)
  // {
  //   //dont try to update invalid bounds
  //   if (!object->Mesh->Bounds.Valid)
  //     return;

  //   RenderBounds originalBounds = object->Mesh->Bounds;

  //   //convert bounds to 8 vertices, and transform those
  //   std::array<glm::vec3, 8> boundsVerts;

  //   for (int i = 0; i < 8; i++)
  //   {
  //     boundsVerts[i] = originalBounds.origin;
  //   }

  //   boundsVerts[0] += originalBounds.extents * glm::vec3(1, 1, 1);
  //   boundsVerts[1] += originalBounds.extents * glm::vec3(1, 1, -1);
  //   boundsVerts[2] += originalBounds.extents * glm::vec3(1, -1, 1);
  //   boundsVerts[3] += originalBounds.extents * glm::vec3(1, -1, -1);
  //   boundsVerts[4] += originalBounds.extents * glm::vec3(-1, 1, 1);
  //   boundsVerts[5] += originalBounds.extents * glm::vec3(-1, 1, -1);
  //   boundsVerts[6] += originalBounds.extents * glm::vec3(-1, -1, 1);
  //   boundsVerts[7] += originalBounds.extents * glm::vec3(-1, -1, -1);

  //   //recalc max/min
  //   glm::vec3 min{std::numeric_limits<float>().max()};
  //   glm::vec3 max{-std::numeric_limits<float>().max()};

  //   glm::mat4 m = object->transformMatrix;

  //   //transform every vertex, accumulating max/min
  //   for (int i = 0; i < 8; i++)
  //   {
  //     boundsVerts[i] = m * glm::vec4(boundsVerts[i], 1.f);

  //     min = glm::min(boundsVerts[i], min);
  //     max = glm::max(boundsVerts[i], max);
  //   }

  //   glm::vec3 extents = (max - min) / 2.f;
  //   glm::vec3 origin = min + extents;

  //   float max_scale = 0;
  //   max_scale = std::max(glm::length(glm::vec3(m[0][0], m[0][1], m[0][2])), max_scale);
  //   max_scale = std::max(glm::length(glm::vec3(m[1][0], m[1][1], m[1][2])), max_scale);
  //   max_scale = std::max(glm::length(glm::vec3(m[2][0], m[2][1], m[2][2])), max_scale);

  //   float radius = max_scale * originalBounds.radius;

  //   object->bounds.extents = extents;
  //   object->bounds.origin = origin;
  //   object->bounds.radius = radius;
  //   object->bounds.valid = true;
  // }

  template <typename T> T*
  MapBuffer(allocated_buffer<T> &buffer)
  {
    void *data;
    vmaMapMemory(g_Allocator, buffer.Allocation, &data);
    return (T*)data;
  }

  void
  UnmapBuffer(allocated_buffer_untyped &buffer)
  {
    vmaUnmapMemory(g_Allocator, buffer.Allocation);
  }
}

#include "asset/asset.h"
#include "asset/texture_asset.h"

#include "vk_shaders.h"
#include "../material_system.h"
#include "vk_scene.h"

#include "vk_textures.h"

#include "vk_scenerender.cpp"

namespace vk::renderer
{

global_variable pipeline_builder g_PipelineBuilder {};

void
ReadyCullData(render_scene::mesh_pass& pass, VkCommandBuffer cmd)
{
	//copy from the cleared indirect buffer into the one we will use on rendering. This one happens every frame
	VkBufferCopy indirectCopy;
	indirectCopy.dstOffset = 0;
	indirectCopy.size = pass.Batches.size() * sizeof(gpu_indirect_object);
	indirectCopy.srcOffset = 0;
	vkCmdCopyBuffer(cmd, pass.ClearIndirectBuffer.Buffer, pass.DrawIndirectBuffer.Buffer, 1, &indirectCopy);

	{
		VkBufferMemoryBarrier barrier = vk::init::buffer_barrier(pass.DrawIndirectBuffer.Buffer, g_RenderState.GraphicsQueueFamily);

		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		g_RenderState.CullReadyBarriers.push_back(barrier);
		//vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);
	}
}

allocated_buffer_untyped
CreateBuffer(u64 allocSize,
             VkBufferUsageFlags usage,
             VmaMemoryUsage memoryUsage,
             VkMemoryPropertyFlags requiredFlags)
{
  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.pNext = nullptr;

  bufferInfo.size = allocSize;
  bufferInfo.usage = usage;

  
  VmaAllocationCreateInfo vmaallocInfo = {};
  vmaallocInfo.usage = memoryUsage;
  vmaallocInfo.requiredFlags = requiredFlags;

  allocated_buffer_untyped newBuffer;

  VK_CHECK(vmaCreateBuffer(g_Allocator, &bufferInfo, &vmaallocInfo,
    &newBuffer.Buffer,
    &newBuffer.Allocation,
    nullptr));

  newBuffer.Size = allocSize;

  return newBuffer;
}

void
ReallocateBuffer(allocated_buffer_untyped& buffer,
                 size_t allocSize,
                 VkBufferUsageFlags usage,
                 VmaMemoryUsage memoryUsage,
                 VkMemoryPropertyFlags required_flags)
{
  allocated_buffer_untyped newBuffer = CreateBuffer(allocSize, usage, memoryUsage, required_flags);

	g_RenderState.GetCurrentFrame().FrameDeletionQueue.push_function([=]()
  {
		vmaDestroyBuffer(g_Allocator, buffer.Buffer, buffer.Allocation);
	});

	buffer = newBuffer;
}

u64
PadUniformBufferSize(u64 originalSize)
{
  u64 minUboAlignment = g_PlatformState.GpuProperties.limits.minUniformBufferOffsetAlignment;
  u64 alignedSize = originalSize;
  if (minUboAlignment > 0)
  {
    alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
  }
  return alignedSize;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageType,
               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
               void*) 
{
  auto ms = vkb::to_string_message_severity(messageSeverity);
  auto mt = vkb::to_string_message_type(messageType);

  if (messageSeverity == VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
  {
    logger::err("[%s] %s", mt, pCallbackData->pMessage);
  }
  else
  if (messageSeverity == VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
  {
    logger::warn("[%s] %s", mt, pCallbackData->pMessage);
  }
  else
  {
    logger::log("[%s][%s] %s",ms, mt, pCallbackData->pMessage);
  }

  return VK_FALSE;
}

struct indirect_batch
{
  Mesh* Mesh;
  vk::util::material* Material;
  u32 First;
  u32 Count;
};

std::vector<indirect_batch>
CompactDraws(RenderObject* objects,
             int count)
{
  std::vector<indirect_batch> draws;

  indirect_batch firstDraw;
  firstDraw.Mesh = objects[0].Mesh;
  firstDraw.Material = objects[0].Material;
  firstDraw.First = 0;
  firstDraw.Count = 1;

  draws.push_back(firstDraw);

  for(int i = 0; i < count; i++)
  {
    bool sameMesh = objects[i].Mesh == draws.back().Mesh;
    bool sameMaterial = objects[i].Material == draws.back().Material;

    if (sameMesh && sameMaterial)
    {
      draws.back().Count++;
    }
    else
    {
      indirect_batch newDraw;
      firstDraw.Mesh = objects[i].Mesh;
      firstDraw.Material = objects[i].Material;
      firstDraw.First = i;
      firstDraw.Count = 1;

      draws.push_back(newDraw);
    }
  }

  return draws;
}

internal vulkan_platform_state
Win32InitVulkan(HWND WindowWin32,
                HINSTANCE InstanceWin32)
{
  g_PlatformState.Win32NativeWindow = WindowWin32;

  vkb::InstanceBuilder builder;
  auto inst_ret = builder.set_app_name("VulkanEngine")
    .request_validation_layers(true)
    .require_api_version(1,1,0)
    .use_default_debug_messenger()
    .set_debug_callback(debug_callback)
    .build();

  vkb::Instance vkb_inst = inst_ret.value();

  VkSurfaceKHR Surface = VK_NULL_HANDLE;
  {
    VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo = {};
    SurfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    SurfaceCreateInfo.pNext = nullptr;
    SurfaceCreateInfo.flags = 0;
    SurfaceCreateInfo.hinstance = InstanceWin32;
    SurfaceCreateInfo.hwnd = WindowWin32;
    VkResult Result = vkCreateWin32SurfaceKHR(vkb_inst.instance, &SurfaceCreateInfo, nullptr, &Surface);
    Assert(Result == VK_SUCCESS);
  }


  vkb::PhysicalDeviceSelector selector { vkb_inst };
  vkb::PhysicalDevice physicalDevice = selector
    .set_minimum_version(1,1)
    .set_surface(Surface)
    .select()
    .value();

  vkb::DeviceBuilder deviceBuilder { physicalDevice };
  vkb::Device vkbDevice = deviceBuilder.build().value();

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice.physical_device, Surface, &surfaceCapabilities);
  g_PlatformState.WindowExtent = surfaceCapabilities.currentExtent;

  g_PlatformState.Device = vkbDevice.device;
  g_PlatformState.ChosenGPU = physicalDevice.physical_device;
  g_PlatformState.Instance = vkb_inst.instance;
  g_PlatformState.Surface = Surface;
  g_PlatformState.DebugMessenger = vkb_inst.debug_messenger;

  g_RenderState.GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  g_RenderState.GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.physicalDevice = g_PlatformState.ChosenGPU;
  allocatorInfo.device = g_PlatformState.Device;
  allocatorInfo.instance = g_PlatformState.Instance;
  allocatorInfo.pAllocationCallbacks = g_VkAllocator;
  vmaCreateAllocator(&allocatorInfo, &g_Allocator);

  vkGetPhysicalDeviceProperties(g_PlatformState.ChosenGPU, &g_PlatformState.GpuProperties);

  return g_PlatformState;
}

u32
PreviousPow2(u32 v)
{
	u32 r = 1;

	while (r * 2 < v)
		r *= 2;

	return r;
}

u32
GetImageMipLevels(u32 width, u32 height)
{
	u32 result = 1;

	while (width > 1 || height > 1)
	{
		result++;
		width /= 2;
		height /= 2;
	}

	return result;
}

internal void
InitSwapchain()
{
  vkb::SwapchainBuilder swapchainBuilder { g_PlatformState.ChosenGPU, g_PlatformState.Device, g_PlatformState.Surface };

  vkb::Swapchain vkbSwapchain = swapchainBuilder
    .use_default_format_selection()
    .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR) // VK_PRESENT_MODE_IMMEDIATE_KHR / VK_PRESENT_MODE_FIFO_KHR
    .set_desired_extent(g_PlatformState.WindowExtent.width, g_PlatformState.WindowExtent.height)
    .build()
    .value();

  g_RenderState.Swapchain = vkbSwapchain.swapchain;  
  g_RenderState.SwapchainImages = vkbSwapchain.get_images().value();
  g_RenderState.SwapchainImageViews = vkbSwapchain.get_image_views().value();
  g_RenderState.SwapchainImageFormat = vkbSwapchain.image_format;

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroySwapchainKHR(g_PlatformState.Device, g_RenderState.Swapchain, nullptr);
  });

  // render image
  {
    VkExtent3D renderImageExtent =
    {
			g_PlatformState.WindowExtent.width,
      g_PlatformState.WindowExtent.height,
			1
		};

		g_RenderState.RenderFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		VkImageCreateInfo ri_info = vk::init::image_create_info(g_RenderState.RenderFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT| VK_IMAGE_USAGE_SAMPLED_BIT, renderImageExtent);

		//for the depth image, we want to allocate it from gpu local memory
		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//allocate and create the image
		vmaCreateImage(g_Allocator, &ri_info, &dimg_allocinfo, &g_RenderState.RawRenderImage.Image, &g_RenderState.RawRenderImage.Allocation, nullptr);

		//build a image-view for the depth image to use for rendering
		VkImageViewCreateInfo dview_info = vk::init::imageview_create_info(g_RenderState.RenderFormat, g_RenderState.RawRenderImage.Image, VK_IMAGE_ASPECT_COLOR_BIT);

		VK_CHECK(vkCreateImageView(g_PlatformState.Device, &dview_info, nullptr, &g_RenderState.RawRenderImage.DefaultView));
  }

  //depth image size will match the window
	VkExtent3D depthImageExtent =
  {
		g_PlatformState.WindowExtent.width,
		g_PlatformState.WindowExtent.height,
		1
	};

	VkExtent3D shadowExtent =
  {
		g_RenderState.ShadowExtent.width,
		g_RenderState.ShadowExtent.height,
		1
	};

	//hardcoding the depth format to 32 bit float
	g_RenderState.DepthFormat = VK_FORMAT_D32_SFLOAT;

	//for the depth image, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // depth image ------ 
	{
		//the depth image will be a image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vk::init::image_create_info(g_RenderState.DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, depthImageExtent);


		//allocate and create the image
		vmaCreateImage(g_Allocator, &dimg_info, &dimg_allocinfo, &g_RenderState.DepthImage.Image, &g_RenderState.DepthImage.Allocation, nullptr);


		//build a image-view for the depth image to use for rendering
		VkImageViewCreateInfo dview_info = vk::init::imageview_create_info(g_RenderState.DepthFormat, g_RenderState.DepthImage.Image, VK_IMAGE_ASPECT_DEPTH_BIT);;

		VK_CHECK(vkCreateImageView(g_PlatformState.Device, &dview_info, nullptr, &g_RenderState.DepthImage.DefaultView));
	}

	//shadow image
	{
		//the depth image will be a image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vk::init::image_create_info(g_RenderState.DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowExtent);

		//allocate and create the image
		vmaCreateImage(g_Allocator, &dimg_info, &dimg_allocinfo, &g_RenderState.ShadowImage.Image, &g_RenderState.ShadowImage.Allocation, nullptr);

		//build a image-view for the depth image to use for rendering
		VkImageViewCreateInfo dview_info = vk::init::imageview_create_info(g_RenderState.DepthFormat, g_RenderState.ShadowImage.Image, VK_IMAGE_ASPECT_DEPTH_BIT);

		VK_CHECK(vkCreateImageView(g_PlatformState.Device, &dview_info, nullptr, &g_RenderState.ShadowImage.DefaultView));
	}

  // Note: previousPow2 makes sure all reductions are at most by 2x2 which makes sure they are conservative
	g_RenderState.DepthPyramidWidth = PreviousPow2(g_PlatformState.WindowExtent.width);
	g_RenderState.DepthPyramidHeight = PreviousPow2(g_PlatformState.WindowExtent.height);
	g_RenderState.DepthPyramidLevels = GetImageMipLevels(g_RenderState.DepthPyramidWidth, g_RenderState.DepthPyramidHeight);

	VkExtent3D pyramidExtent =
  {
		static_cast<u32>(g_RenderState.DepthPyramidWidth),
		static_cast<u32>(g_RenderState.DepthPyramidHeight),
		1
	};

	//the depth image will be a image with the format we selected and Depth Attachment usage flag
	VkImageCreateInfo pyramidInfo = vk::init::image_create_info(VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, pyramidExtent);

	pyramidInfo.mipLevels = g_RenderState.DepthPyramidLevels;

	//allocate and create the image
	vmaCreateImage(g_Allocator, &pyramidInfo, &dimg_allocinfo, &g_RenderState.DepthPyramid.Image, &g_RenderState.DepthPyramid.Allocation, nullptr);

	//build a image-view for the depth image to use for rendering
	VkImageViewCreateInfo priview_info = vk::init::imageview_create_info(VK_FORMAT_R32_SFLOAT, g_RenderState.DepthPyramid.Image, VK_IMAGE_ASPECT_COLOR_BIT);
	priview_info.subresourceRange.levelCount = g_RenderState.DepthPyramidLevels;


	VK_CHECK(vkCreateImageView(g_PlatformState.Device, &priview_info, nullptr, &g_RenderState.DepthPyramid.DefaultView));

  for (i32 i = 0; i < g_RenderState.DepthPyramidLevels; ++i)
	{
		VkImageViewCreateInfo level_info = vk::init::imageview_create_info(VK_FORMAT_R32_SFLOAT, g_RenderState.DepthPyramid.Image, VK_IMAGE_ASPECT_COLOR_BIT);
		level_info.subresourceRange.levelCount = 1;
		level_info.subresourceRange.baseMipLevel = i;

		VkImageView pyramid;
		vkCreateImageView(g_PlatformState.Device, &level_info, nullptr, &pyramid);

		g_RenderState.DepthPyramidMips[i] = pyramid;
		Assert(g_RenderState.DepthPyramidMips[i]);
	}

  VkSamplerCreateInfo createInfo = {};

	auto reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN;

	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.magFilter = VK_FILTER_LINEAR;
	createInfo.minFilter = VK_FILTER_LINEAR;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.minLod = 0;
	createInfo.maxLod = 16.f;

	VkSamplerReductionModeCreateInfoEXT createInfoReduction = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };

	if (reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT)
	{
		createInfoReduction.reductionMode = reductionMode;

		createInfo.pNext = &createInfoReduction;
	}

	VK_CHECK(vkCreateSampler(g_PlatformState.Device, &createInfo, 0, &g_RenderState.DepthSampler));

  VkSamplerCreateInfo samplerInfo = vk::init::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	
	vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &g_RenderState.SmoothSampler);

	VkSamplerCreateInfo shadsamplerInfo = vk::init::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
	shadsamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	shadsamplerInfo.compareEnable = true;
	shadsamplerInfo.compareOp = VK_COMPARE_OP_LESS;
	vkCreateSampler(g_PlatformState.Device, &shadsamplerInfo, nullptr, &g_RenderState.ShadowSampler);


	//add to deletion queues
	g_RenderState.MainDeletionQueue.push_function([=]()
  {
		vkDestroyImageView(g_PlatformState.Device, g_RenderState.DepthImage.DefaultView, nullptr);
		vmaDestroyImage(g_Allocator, g_RenderState.DepthImage.Image, g_RenderState.DepthImage.Allocation);
	});
}

internal void
InitCommands()
{
  VkCommandPoolCreateInfo commandPoolInfo = vk::init::command_pool_create_info(g_RenderState.GraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  for (int i = 0; i < FRAME_OVERLAP; i++)
  {
    VK_CHECK(vkCreateCommandPool(g_PlatformState.Device, &commandPoolInfo, nullptr, &g_RenderState.Frames[i].CommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vk::init::command_buffer_allocate_info(g_RenderState.Frames[i].CommandPool, 1);

    VK_CHECK(vkAllocateCommandBuffers(g_PlatformState.Device, &cmdAllocInfo, &g_RenderState.Frames[i].MainCommandBuffer));

    g_RenderState.MainDeletionQueue.push_function([=]()
    {
      vkDestroyCommandPool(g_PlatformState.Device, g_RenderState.Frames[i].CommandPool, nullptr);
    });
  }

  VkCommandPoolCreateInfo uploadCommandPoolInfo = vk::init::command_pool_create_info(g_RenderState.GraphicsQueueFamily);

  VK_CHECK(vkCreateCommandPool(g_PlatformState.Device, &uploadCommandPoolInfo, nullptr, &g_RenderState.UploadContext.CommandPool));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyCommandPool(g_PlatformState.Device, g_RenderState.UploadContext.CommandPool, nullptr);
  });
}

// internal void
// InitDefaultRenderpass()
// {
//   VkAttachmentDescription color_attachment = {};
//   color_attachment.format = g_RenderState.SwapchainImageFormat;
//   color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
//   color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
//   color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
//   color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  
//   color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
//   color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//   color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

//   VkAttachmentReference color_attachment_ref = {};
//   color_attachment_ref.attachment = 0;
//   color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

//   VkAttachmentDescription depth_attachment = {};
//   depth_attachment.flags = 0;
//   depth_attachment.format = g_RenderState.DepthFormat;
//   depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
//   depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
//   depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
//   depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
//   depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
//   depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//   depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

//   VkAttachmentReference depth_attachment_ref = {};
//   depth_attachment_ref.attachment = 1;
//   depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

//   VkSubpassDescription subpass = {};
//   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
//   subpass.colorAttachmentCount = 1;
//   subpass.pColorAttachments = &color_attachment_ref;
//   subpass.pDepthStencilAttachment = &depth_attachment_ref;

//   VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

//   VkRenderPassCreateInfo render_pass_info = {};
//   render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
//   render_pass_info.attachmentCount = 2;
//   render_pass_info.pAttachments = &attachments[0];
//   render_pass_info.subpassCount = 1;
//   render_pass_info.pSubpasses = &subpass;
  
//   VK_CHECK(vkCreateRenderPass(g_PlatformState.Device, &render_pass_info, nullptr, &g_RenderState.RenderPass));

//   g_RenderState.MainDeletionQueue.push_function([=]()
//   {
//     vkDestroyRenderPass(g_PlatformState.Device, g_RenderState.RenderPass, nullptr);
//   });
// }

internal void
InitForwardRenderpass()
{
  VkAttachmentDescription color_attachment = {};
	color_attachment.format = g_RenderState.SwapchainImageFormat;// _renderFormat;//_swachainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;//PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = g_RenderState.DepthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	//hook the depth attachment into the subpass
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


	//array of 2 attachments, one for the color, and other for depth
	VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	//render_pass_info.dependencyCount = 1;
	//render_pass_info.pDependencies = &dependency;

  VK_CHECK(vkCreateRenderPass(g_PlatformState.Device, &render_pass_info, nullptr, &g_RenderState.RenderPass));

	g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyRenderPass(g_PlatformState.Device, g_RenderState.RenderPass, nullptr);
	});
}

internal void
InitCopyRenderpass()
{
  VkAttachmentDescription color_attachment = {};
	color_attachment.format = g_RenderState.SwapchainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;	

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	//render_pass_info.dependencyCount = 1;
	//render_pass_info.pDependencies = &dependency;

  VK_CHECK(vkCreateRenderPass(g_PlatformState.Device, &render_pass_info, nullptr, &g_RenderState.CopyPass));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyRenderPass(g_PlatformState.Device, g_RenderState.CopyPass, nullptr);
  });
}

internal void
InitShadowRenderpass()
{
  VkAttachmentDescription depth_attachment = {};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = g_RenderState.DepthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 0;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

	//hook the depth attachment into the subpass
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	//1 dependency, which is from "outside" into the subpass. And we can read or write color
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &depth_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

  VK_CHECK(vkCreateRenderPass(g_PlatformState.Device, &render_pass_info, nullptr, &g_RenderState.ShadowPass));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyRenderPass(g_PlatformState.Device, g_RenderState.ShadowPass, nullptr);
  });
}

internal void
InitFramebuffers()
{
  const u32 swapchain_imagecount = static_cast<u32>(g_RenderState.SwapchainImages.size());
	g_RenderState.Framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
	VkFramebufferCreateInfo fwd_info = vk::init::framebuffer_create_info(g_RenderState.RenderPass, g_PlatformState.WindowExtent);
	VkImageView attachments[2];
	attachments[0] = g_RenderState.RawRenderImage.DefaultView;
	attachments[1] = g_RenderState.DepthImage.DefaultView;

	fwd_info.pAttachments = attachments;
	fwd_info.attachmentCount = 2;
	VK_CHECK(vkCreateFramebuffer(g_PlatformState.Device, &fwd_info, nullptr, &g_RenderState.ForwardFramebuffer));

	//create the framebuffer for shadow pass	
	VkFramebufferCreateInfo sh_info = vk::init::framebuffer_create_info(g_RenderState.ShadowPass, g_RenderState.ShadowExtent);
	sh_info.pAttachments = &g_RenderState.ShadowImage.DefaultView;
	sh_info.attachmentCount = 1;
	VK_CHECK(vkCreateFramebuffer(g_PlatformState.Device, &sh_info, nullptr, &g_RenderState.ShadowFramebuffer));
	
	for (uint32_t i = 0; i < swapchain_imagecount; i++)
  {
		VkFramebufferCreateInfo fb_info = vk::init::framebuffer_create_info(g_RenderState.CopyPass, g_PlatformState.WindowExtent);
		fb_info.pAttachments = &g_RenderState.SwapchainImageViews[i];
		fb_info.attachmentCount = 1;
		VK_CHECK(vkCreateFramebuffer(g_PlatformState.Device, &fb_info, nullptr, &g_RenderState.Framebuffers[i]));

		g_RenderState.MainDeletionQueue.push_function([=]()
    {
			vkDestroyFramebuffer(g_PlatformState.Device, g_RenderState.Framebuffers[i], nullptr);
			vkDestroyImageView(g_PlatformState.Device, g_RenderState.SwapchainImageViews[i], nullptr);
		});
  }
}

internal void
InitSyncStructures()
{
  VkFenceCreateInfo fenceCreateInfo = vk::init::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
  VkSemaphoreCreateInfo semaphoreCreateInfo = vk::init::semaphore_create_info();

  for(i32 i = 0; i < FRAME_OVERLAP; i++)
  {
    VK_CHECK(vkCreateFence(g_PlatformState.Device, &fenceCreateInfo, nullptr, &g_RenderState.Frames[i].RenderFence));
    g_RenderState.MainDeletionQueue.push_function([=]()
    {
      vkDestroyFence(g_PlatformState.Device, g_RenderState.Frames[i].RenderFence, nullptr);
    });

    VK_CHECK(vkCreateSemaphore(g_PlatformState.Device, &semaphoreCreateInfo, nullptr, &g_RenderState.Frames[i].PresentSemaphore));
    VK_CHECK(vkCreateSemaphore(g_PlatformState.Device, &semaphoreCreateInfo, nullptr, &g_RenderState.Frames[i].RenderSemaphore));

    g_RenderState.MainDeletionQueue.push_function([=]()
    {
      vkDestroySemaphore(g_PlatformState.Device, g_RenderState.Frames[i].PresentSemaphore, nullptr);
      vkDestroySemaphore(g_PlatformState.Device, g_RenderState.Frames[i].RenderSemaphore, nullptr);
    });
  }

  VkFenceCreateInfo uploadFenceCreateInfo = vk::init::fence_create_info();

  VK_CHECK(vkCreateFence(g_PlatformState.Device, &uploadFenceCreateInfo, nullptr, &g_RenderState.UploadContext.UploadFence));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyFence(g_PlatformState.Device, g_RenderState.UploadContext.UploadFence, nullptr);
  });
}

internal void
InitDescriptors()
{
  g_RenderState.DescriptorAllocator = new vk::descriptor::allocator {};
	//_descriptorAllocator->init(_device);

	g_RenderState.DescriptorLayoutCache = new vk::descriptor::cache {};
	//g_RenderState.DescriptorLayoutCache->init(_device);

	VkDescriptorSetLayoutBinding textureBind = vk::init::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3info = {};
	set3info.bindingCount = 1;
	set3info.flags = 0;
	set3info.pNext = nullptr;
	set3info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3info.pBindings = &textureBind;

	g_RenderState.SingleTextureSetLayout = g_RenderState.DescriptorLayoutCache->CreateDescriptorLayout(&set3info);

	const size_t sceneParamBufferSize = FRAME_OVERLAP * PadUniformBufferSize(sizeof(GPUSceneData));

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		g_RenderState.Frames[i].DynamicDescriptorAllocator = new vk::descriptor::allocator {};
		// g_RenderState.Frames[i].DynamicDescriptorAllocator->init(_device);

		//1 megabyte of dynamic data buffer
		auto dynamicDataBuffer = CreateBuffer(1000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
		g_RenderState.Frames[i].DynamicData.Init(g_Allocator, dynamicDataBuffer, (u32)g_PlatformState.GpuProperties.limits.minUniformBufferOffsetAlignment); 

		//20 megabyte of debug output
		g_RenderState.Frames[i].DebugOutputBuffer = CreateBuffer(200000000, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
	}
}

// internal void
// DrawObjects(VkCommandBuffer cmd,
//             RenderObject* first,
//             u64 count)
// {
//   FrameData& currentFrame = g_RenderState.GetCurrentFrame();

//   glm::vec3 camPos = { 0.f,-2.f,-10.f };

//   glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
//   glm::mat4 projection = glm::perspective(glm::radians(70.f), (f32)g_PlatformState.WindowExtent.width / g_PlatformState.WindowExtent.height, 0.1f, 200.0f);//1700.f / 900.f, 0.1f, 200.0f);//g_PlatformState.WindowExtent
//   projection[1][1] *= -1;

//   GPUCameraData camData;
//   camData.Projection = projection;
//   camData.View = view;
//   camData.Viewproj = projection * view;

//   void* data;
//   vmaMapMemory(g_Allocator, currentFrame.CameraBuffer.Allocation, &data);

//   memcpy(data, &camData, sizeof(GPUCameraData));

//   vmaUnmapMemory(g_Allocator, currentFrame.CameraBuffer.Allocation);

//   float framed = (g_RenderState.FrameNumber / 120.f);

//   g_RenderState.SceneParameters.AmbientColor = { sin(framed),0,cos(framed),1 };

//   char* sceneData;
//   vmaMapMemory(g_Allocator, g_RenderState.SceneParameterBuffer.Allocation , (void**)&sceneData);

//   int frameIndex = g_RenderState.FrameNumber % FRAME_OVERLAP;

//   sceneData += PadUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;

//   memcpy(sceneData, &g_RenderState.SceneParameters, sizeof(GPUSceneData));

//   vmaUnmapMemory(g_Allocator, g_RenderState.SceneParameterBuffer.Allocation);

//   void* objectData;

//   vmaMapMemory(g_Allocator, currentFrame.ObjectBuffer.Allocation, &objectData);

//   GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

//   for (int i = 0; i < count; i++)
//   {
//     RenderObject& object = first[i];
//     objectSSBO[i].ModelMatrix = object.TransformMatrix;
//   }

//   vmaUnmapMemory(g_Allocator, currentFrame.ObjectBuffer.Allocation);

//   Mesh* lastMesh = nullptr;
//   vk::util::material* lastMaterial = nullptr;

//   std::vector<indirect_batch> draws = CompactDraws(first, (int)count);

//   for(indirect_batch& draw : draws)
//   {
//     if (draw.Material != lastMaterial)
//     {
//       vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->Pipeline);
//       vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 0, 1, &currentFrame.GlobalDescriptor, 0, nullptr);
//       vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 1, 1, &currentFrame.ObjectDescriptor, 0, nullptr);
//       if (draw.Material->TextureSet != VK_NULL_HANDLE)
//       {
//         vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 2, 1, &draw.Material->TextureSet, 0, nullptr);
//       }

//       lastMaterial = draw.Material;
//     }

//     //MeshPushConstants constants;
//     //constants.RenderMatrix = draw.TransformMatrix;
//     //vkCmdPushConstants(cmd, draw.Material->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

//     if (draw.Mesh != lastMesh)
//     {
//       VkDeviceSize offset = 0;
//       vkCmdBindVertexBuffers(cmd, 0, 1, &draw.Mesh->VertexBuffer.Buffer, &offset);
//       lastMesh = draw.Mesh;
//     }

//     for(u32 i = draw.First; i < draw.Count; i++)
//     {
//       vkCmdDraw(cmd, (u32)draw.Mesh->Vertices.size(), 1, 0, (u32)i);
//     }
//   }

//   // for (u64 i = 0; i < count; i++)
//   // {
//   //   RenderObject& object = first[i];

//   //   if (object.Material != lastMaterial)
//   //   {
//   //     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->Pipeline);
//   //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 0, 1, &currentFrame.GlobalDescriptor, 0, nullptr);
//   //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 1, 1, &currentFrame.ObjectDescriptor, 0, nullptr);
//   //     if (object.Material->TextureSet != VK_NULL_HANDLE)
//   //     {
//   //       vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 2, 1, &object.Material->TextureSet, 0, nullptr);
//   //     }

//   //     lastMaterial = object.Material;
//   //   }

//   //   MeshPushConstants constants;
//   //   constants.RenderMatrix = object.TransformMatrix;
//   //   vkCmdPushConstants(cmd, object.Material->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

//   //   if (object.Mesh != lastMesh)
//   //   {
//   //     VkDeviceSize offset = 0;
//   //     vkCmdBindVertexBuffers(cmd, 0, 1, &object.Mesh->VertexBuffer.Buffer, &offset);
//   //     lastMesh = object.Mesh;
//   //   }
    
//   //   vkCmdDraw(cmd, (u32)object.Mesh->Vertices.size(), 1, 0, (u32)i);
//   // }
// }

void
ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
  VkCommandBufferAllocateInfo cmdAllocInfo = vk::init::command_buffer_allocate_info(g_RenderState.UploadContext.CommandPool, 1);

  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(g_PlatformState.Device, &cmdAllocInfo, &cmd));

  VkCommandBufferBeginInfo cmdBeginInfo = vk::init::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

  function(cmd);

  VK_CHECK(vkEndCommandBuffer(cmd));

  VkSubmitInfo submit = vk::init::submit_info(&cmd);

  VK_CHECK(vkQueueSubmit(g_RenderState.GraphicsQueue, 1, &submit, g_RenderState.UploadContext.UploadFence));

  vkWaitForFences(g_PlatformState.Device, 1, &g_RenderState.UploadContext.UploadFence, true, 9999999999);
  vkResetFences(g_PlatformState.Device, 1, &g_RenderState.UploadContext.UploadFence);

  vkResetCommandPool(g_PlatformState.Device, g_RenderState.UploadContext.CommandPool, 0);
}

// internal void
// Draw()
// {
//   FrameData& currentFrame = g_RenderState.GetCurrentFrame();

//   VK_CHECK(vkWaitForFences(g_PlatformState.Device, 1, &currentFrame.RenderFence, true, 1000000000));
//   VK_CHECK(vkResetFences(g_PlatformState.Device, 1, &currentFrame.RenderFence));

//   u32 swapchainImageIndex;
//   VK_CHECK(vkAcquireNextImageKHR(g_PlatformState.Device, g_RenderState.Swapchain, 1000000000, currentFrame.PresentSemaphore, nullptr, &swapchainImageIndex));

//   VK_CHECK(vkResetCommandBuffer(currentFrame.MainCommandBuffer, 0));

//   VkCommandBuffer cmd = currentFrame.MainCommandBuffer;

//   VkCommandBufferBeginInfo cmdBeginInfo = {};
//   cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//   cmdBeginInfo.pNext = nullptr;
//   cmdBeginInfo.pInheritanceInfo = nullptr;
//   cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

//   VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

//   VkClearValue clearValue;
//   float flash = abs(sin(g_RenderState.FrameNumber / 120.f));
//   clearValue.color = { { 0.0f, 0.0f, flash * 0.5f, 1.0f } };

//   VkClearValue depthClear;
//   depthClear.depthStencil.depth = 1.f;

//   VkRenderPassBeginInfo rpInfo = vk::init::renderpass_begin_info(g_RenderState.RenderPass, g_PlatformState.WindowExtent, g_RenderState.Framebuffers[swapchainImageIndex]);

//   rpInfo.clearValueCount = 2;

//   VkClearValue clearValues[] = { clearValue, depthClear };

//   rpInfo.pClearValues = &clearValues[0];

//   vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

//   DrawObjects(cmd, g_RenderState.Renderables.data(), g_RenderState.Renderables.size());

//   ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

//   vkCmdEndRenderPass(cmd);
//   VK_CHECK(vkEndCommandBuffer(cmd));

//   VkSubmitInfo submit = {};
//   submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//   submit.pNext = nullptr;

//   VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

//   submit.pWaitDstStageMask = &waitStage;

//   submit.waitSemaphoreCount = 1;
//   submit.pWaitSemaphores = &currentFrame.PresentSemaphore;

//   submit.signalSemaphoreCount = 1;
//   submit.pSignalSemaphores = &currentFrame.RenderSemaphore;

//   submit.commandBufferCount = 1;
//   submit.pCommandBuffers = &cmd;

//   VK_CHECK(vkQueueSubmit(g_RenderState.GraphicsQueue, 1, &submit, currentFrame.RenderFence));

//   VkPresentInfoKHR presentInfo = {};
//   presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
//   presentInfo.pNext = nullptr;

//   presentInfo.pSwapchains = &g_RenderState.Swapchain;
//   presentInfo.swapchainCount = 1;

//   presentInfo.pWaitSemaphores = &currentFrame.RenderSemaphore;
//   presentInfo.waitSemaphoreCount = 1;

//   presentInfo.pImageIndices = &swapchainImageIndex;

//   VK_CHECK(vkQueuePresentKHR(g_RenderState.GraphicsQueue, &presentInfo));

//   g_RenderState.FrameNumber++;
// }

void
ForwardPass(VkClearValue clearValue, VkCommandBuffer cmd)
{
	//vkutil::VulkanScopeTimer timer(cmd, _profiler, "Forward Pass");
	//vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Forward Primitives");
	//clear depth at 0
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 0.f;

	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo rpInfo = vk::init::renderpass_begin_info(g_RenderState.RenderPass, g_PlatformState.WindowExtent, g_RenderState.ForwardFramebuffer/*_framebuffers[swapchainImageIndex]*/);

	//connect clear values
	rpInfo.clearValueCount = 2;

	VkClearValue clearValues[] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (f32)g_PlatformState.WindowExtent.width;
	viewport.height = (f32)g_PlatformState.WindowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = g_PlatformState.WindowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	vkCmdSetDepthBias(cmd, 0, 0, 0);


	//stats.drawcalls = 0;
	//stats.draws = 0;
	//stats.objects = 0;
	//stats.triangles = 0;

	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Forward Pass");
		DrawObjectsForward(cmd, g_RenderState.RenderScene->ForwardPass);
		DrawObjectsForward(cmd, g_RenderState.RenderScene->TransparentForwardPass);
	}


	{
		//TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Imgui Draw");
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}


void
ShadowPass(VkCommandBuffer cmd)
{
	//vkutil::VulkanScopeTimer timer(cmd, _profiler, "Shadow Pass");
	//vkutil::VulkanPipelineStatRecorder timer2(cmd, _profiler, "Shadow Primitives");
	if (FREEZE_SHADOWS) return;

	if (!SHADOWCAST)
	{
		return;
	}

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;	
	VkRenderPassBeginInfo rpInfo = vk::init::renderpass_begin_info(g_RenderState.ShadowPass, g_RenderState.ShadowExtent, g_RenderState.ShadowFramebuffer);

	//connect clear values
	rpInfo.clearValueCount = 1;

	VkClearValue clearValues[] = { depthClear };

	rpInfo.pClearValues = &clearValues[0];
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (f32)g_RenderState.ShadowExtent.width;
	viewport.height = (f32)g_RenderState.ShadowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = g_RenderState.ShadowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	


	g_RenderState.stats.drawcalls = 0;
	g_RenderState.stats.draws = 0;
	g_RenderState.stats.objects = 0;
	g_RenderState.stats.triangles = 0;

	if(g_RenderState.RenderScene->ShadowPass.Batches.size() > 0)
	{
		//TracyVkZone(g_RenderState.GraphicsQueueContext, g_RenderState.GetCurrentFrame().MainCommandBuffer, "Shadow  Pass");
		DrawObjectsShadow(cmd, g_RenderState.RenderScene->ShadowPass);
	}

	//finalize the render pass
	vkCmdEndRenderPass(cmd);
}

void
CopyRenderToSwapchain(u32 swapchainImageIndex, VkCommandBuffer cmd)
{
	//start the main renderpass. 
	//We will use the clear color from above, and the framebuffer of the index the swapchain gave us
	VkRenderPassBeginInfo copyRP = vk::init::renderpass_begin_info(g_RenderState.CopyPass, g_PlatformState.WindowExtent, g_RenderState.Framebuffers[swapchainImageIndex]);


	vkCmdBeginRenderPass(cmd, &copyRP, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (f32)g_PlatformState.WindowExtent.width;
	viewport.height = (f32)g_PlatformState.WindowExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor;
	scissor.offset = { 0, 0 };
	scissor.extent = g_PlatformState.WindowExtent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdSetDepthBias(cmd, 0, 0, 0);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_RenderState.BlitPipeline);

	VkDescriptorImageInfo sourceImage;
	sourceImage.sampler = g_RenderState.SmoothSampler;

	sourceImage.imageView = g_RenderState.RawRenderImage.DefaultView;
	sourceImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet blitSet;

  vk::descriptor::builder::descriptor_bind binds[] =
	{
		{ 0, nullptr, &sourceImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, blitSet, binds);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_RenderState.BlitLayout, 0, 1, &blitSet, 0, nullptr);

	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRenderPass(cmd);
}

internal void
Draw2()
{
  FrameData& currentFrame = g_RenderState.GetCurrentFrame();

  {
    //ZoneScopedN("Fence Wait");
    //wait until the gpu has finished rendering the last frame. Timeout of 1 second
    
    VK_CHECK(vkWaitForFences(g_PlatformState.Device, 1, &currentFrame.RenderFence, true, 1000000000));
    VK_CHECK(vkResetFences(g_PlatformState.Device, 1, &currentFrame.RenderFence));

    currentFrame.DynamicData.Reset();

    g_RenderState.RenderScene->BuildBatches();
  }

  currentFrame.FrameDeletionQueue.flush();
  currentFrame.DynamicDescriptorAllocator->ResetPools();

  //now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
  VK_CHECK(vkResetCommandBuffer(currentFrame.MainCommandBuffer, 0));

  uint32_t swapchainImageIndex;
  {
    // ZoneScopedN("Aquire Image");
    //request image from the swapchain

    VK_CHECK(vkAcquireNextImageKHR(g_PlatformState.Device, g_RenderState.Swapchain, 0, currentFrame.PresentSemaphore, nullptr, &swapchainImageIndex));
  }

  //naming it cmd for shorter writing
  VkCommandBuffer cmd = currentFrame.MainCommandBuffer;

  //begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
  VkCommandBufferBeginInfo cmdBeginInfo = vk::init::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

  //make a clear-color from frame number. This will flash with a 120 frame period.
  VkClearValue clearValue;
  float flash = abs(sin(g_RenderState.FrameNumber / 120.f));
  clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}};

  //_profiler->grab_queries(cmd);

  {

    g_RenderState.PostCullBarriers.clear();
    g_RenderState.CullReadyBarriers.clear();

    //TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "All Frame");
    //ZoneScopedNC("Render Frame", tracy::Color::White);

    // vk::util::VulkanScopeTimer timer(cmd, _profiler, "All Frame");

    {
      // vkutil::VulkanScopeTimer timer2(cmd, _profiler, "Ready Frame");

      ReadyMeshDraw(cmd);

      ReadyCullData(g_RenderState.RenderScene->ForwardPass, cmd);
      ReadyCullData(g_RenderState.RenderScene->TransparentForwardPass, cmd);
      ReadyCullData(g_RenderState.RenderScene->ShadowPass, cmd);

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, (u32)g_RenderState.CullReadyBarriers.size(), g_RenderState.CullReadyBarriers.data(), 0, nullptr);
    }

    CullParams forwardCull;
    forwardCull.projmat = g_RenderState.Camera.GetProjectionMatrix(true);
    forwardCull.viewmat = g_RenderState.Camera.GetViewMatrix();
    forwardCull.frustrumCull = true;
    forwardCull.occlusionCull = true;
    forwardCull.drawDist = 5000; // draw distance
    forwardCull.aabb = false;
    {
      ExecuteComputeCull(cmd, g_RenderState.RenderScene->ForwardPass, forwardCull);
      ExecuteComputeCull(cmd, g_RenderState.RenderScene->TransparentForwardPass, forwardCull);
    }

    glm::vec3 extent = g_RenderState.MainLight.shadowExtent * 10.f;
    glm::mat4 projection = glm::orthoLH_ZO(-extent.x, extent.x, -extent.y, extent.y, -extent.z, extent.z);

    CullParams shadowCull;
    shadowCull.projmat = g_RenderState.MainLight.get_projection();
    shadowCull.viewmat = g_RenderState.MainLight.get_view();
    shadowCull.frustrumCull = true;
    shadowCull.occlusionCull = false;
    shadowCull.drawDist = 9999999;
    shadowCull.aabb = true;

    glm::vec3 aabbcenter = g_RenderState.MainLight.lightPosition;
    glm::vec3 aabbextent = g_RenderState.MainLight.shadowExtent * 1.5f;
    shadowCull.aabbmax = aabbcenter + aabbextent;
    shadowCull.aabbmin = aabbcenter - aabbextent;

    {
      // vkutil::VulkanScopeTimer timer2(cmd, _profiler, "Shadow Cull");

      if (SHADOWCAST)
      {
        ExecuteComputeCull(cmd, g_RenderState.RenderScene->ShadowPass, shadowCull);
      }
    }

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, nullptr, (u32)g_RenderState.PostCullBarriers.size(), g_RenderState.PostCullBarriers.data(), 0, nullptr);

    ShadowPass(cmd);

    ForwardPass(clearValue, cmd);

    ReduceDepth(cmd);

    CopyRenderToSwapchain(swapchainImageIndex, cmd);
  }

  // TracyVkCollect(_graphicsQueueContext, get_current_frame()._mainCommandBuffer);

  //finalize the command buffer (we can no longer add commands, but it can now be executed)
  VK_CHECK(vkEndCommandBuffer(cmd));

  //prepare the submission to the queue.
  //we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
  //we will signal the _renderSemaphore, to signal that rendering has finished

  VkSubmitInfo submit = vk::init::submit_info(&cmd);
  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  submit.pWaitDstStageMask = &waitStage;

  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &currentFrame.PresentSemaphore;

  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &currentFrame.RenderSemaphore;
  {
    //ZoneScopedN("Queue Submit");
    //submit command buffer to the queue and execute it.
    // _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit(g_RenderState.GraphicsQueue, 1, &submit, currentFrame.RenderFence));
  }
  //prepare present
  // this will put the image we just rendered to into the visible window.
  // we want to wait on the _renderSemaphore for that,
  // as its necessary that drawing commands have finished before the image is displayed to the user
  VkPresentInfoKHR presentInfo = vk::init::present_info();

  presentInfo.pSwapchains = &g_RenderState.Swapchain;
  presentInfo.swapchainCount = 1;

  presentInfo.pWaitSemaphores = &currentFrame.RenderSemaphore;
  presentInfo.waitSemaphoreCount = 1;

  presentInfo.pImageIndices = &swapchainImageIndex;

  {
    //ZoneScopedN("Queue Present");
    VK_CHECK(vkQueuePresentKHR(g_RenderState.GraphicsQueue, &presentInfo));
  }
  //increase the number of frames drawn
  g_RenderState.FrameNumber++;
}

internal VkShaderModule
LoadShaderModule(const char* filePath)
{
  FILE* file = fopen(filePath, "rb");

  Assert(file)
  
  fseek(file, 0L, SEEK_END);
  u64 fileSize = ftell(file);
  fseek(file, 0L, SEEK_SET);
  std::vector<u32> buffer(fileSize / sizeof(u32));
  fread(buffer.data(), fileSize, 1, file);
  fclose(file);

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.codeSize = buffer.size() * sizeof(u32); 
  createInfo.pCode = buffer.data();
  
  VkShaderModule shaderModule;
  Assert(vkCreateShaderModule(g_PlatformState.Device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

  return shaderModule;
}

VkPipeline
BuildPipeline(VkDevice device, VkRenderPass pass)
{
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.pNext = nullptr;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &g_PipelineBuilder.Viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &g_PipelineBuilder.Scissor;

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.pNext = nullptr;

  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &g_PipelineBuilder.ColorBlendAttachment;

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = nullptr;

  pipelineInfo.stageCount = (u32)g_PipelineBuilder.ShaderStages.size();
  pipelineInfo.pStages = g_PipelineBuilder.ShaderStages.data();
  pipelineInfo.pVertexInputState = &g_PipelineBuilder.VertexInputInfo;
  pipelineInfo.pInputAssemblyState = &g_PipelineBuilder.InputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &g_PipelineBuilder.Rasterizer;
  pipelineInfo.pMultisampleState = &g_PipelineBuilder.Multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = g_PipelineBuilder.PipelineLayout;
  pipelineInfo.renderPass = pass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
  pipelineInfo.pDepthStencilState = &g_PipelineBuilder.DepthStencil;

  VkPipeline newPipeline;
  Assert(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) == VK_SUCCESS);
  return newPipeline;
}

// internal vk::util::material*
// CreateMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
// {
//   vk::util::material mat;
//   mat.Pipeline = pipeline;
//   mat.PipelineLayout = layout;
//   g_RenderState.Materials[name] = mat;
//   return &g_RenderState.Materials[name];
// }

bool
LoadComputeShader(const char* shaderPath,
                  VkPipeline& pipeline,
                  VkPipelineLayout& layout)
{
	shader_module computeModule;
	vk::util::LoadShaderModule(shaderPath, &computeModule);

	shader::shader_effect* computeEffect = new shader::shader_effect();
	computeEffect->AddStage(&computeModule, VK_SHADER_STAGE_COMPUTE_BIT);

	computeEffect->ReflectLayout(nullptr, 0);

	compute_pipeline_builder computeBuilder;
	computeBuilder.PipelineLayout = computeEffect->BuiltLayout;
	computeBuilder.ShaderStage = vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, computeModule.Module);


	layout = computeEffect->BuiltLayout;
	pipeline = computeBuilder.BuildPipeline();

	vkDestroyShaderModule(g_PlatformState.Device, computeModule.Module, nullptr);

	g_RenderState.MainDeletionQueue.push_function([=]()
  {
		vkDestroyPipeline(g_PlatformState.Device, pipeline, nullptr);
		vkDestroyPipelineLayout(g_PlatformState.Device, layout, nullptr);
	});

	return true;
}

internal void
InitPipelines()
{
  g_RenderState.MaterialSystem = new vk::util::material_system();
	// g_RenderState.MaterialSystem->Init(this);
	g_RenderState.MaterialSystem->BuildDefaultTemplates( g_RenderState.ShaderCache, g_RenderState.RenderPass, g_RenderState.ShadowPass );
		
	//fullscreen triangle pipeline for blits
	shader::shader_effect* blitEffect = new shader::shader_effect();
	blitEffect->AddStage(g_RenderState.ShaderCache->GetShader(ShaderPath("fullscreen.vert.spv")), VK_SHADER_STAGE_VERTEX_BIT);
	blitEffect->AddStage(g_RenderState.ShaderCache->GetShader(ShaderPath("blit.frag.spv")), VK_SHADER_STAGE_FRAGMENT_BIT);
	blitEffect->ReflectLayout(nullptr, 0);

	pipeline_builder pipelineBuilder;

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder.InputAssembly = vk::init::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//configure the rasterizer to draw filled triangles
	pipelineBuilder.Rasterizer = vk::init::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder.Rasterizer.cullMode = VK_CULL_MODE_NONE;
	//we dont use multisampling, so just run the default one
	pipelineBuilder.Multisampling = vk::init::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder.ColorBlendAttachment = vk::init::color_blend_attachment_state();


	//default depthtesting
	pipelineBuilder.DepthStencil = vk::init::depth_stencil_create_info(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//build blit pipeline
	pipelineBuilder.SetShaders(blitEffect);

	//blit pipeline uses hardcoded triangle so no need for vertex input
	pipelineBuilder.ClearVertexInput();

	pipelineBuilder.DepthStencil = vk::init::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);

	g_RenderState.BlitPipeline = pipelineBuilder.BuildPipeline(g_RenderState.CopyPass);
	g_RenderState.BlitLayout = blitEffect->BuiltLayout;
	
	g_RenderState.MainDeletionQueue.push_function([=]()
  {
		//vkDestroyPipeline(_device, meshPipeline, nullptr);
		vkDestroyPipeline(g_PlatformState.Device, g_RenderState.BlitPipeline, nullptr);
	});


	//load the compute shaders
	LoadComputeShader(ShaderPath("indirect_cull.comp.spv").c_str(), g_RenderState.CullPipeline, g_RenderState.CullLayout);

	LoadComputeShader(ShaderPath("depthReduce.comp.spv").c_str(), g_RenderState.DepthReducePipeline, g_RenderState.DepthReduceLayout);

	LoadComputeShader(ShaderPath("sparse_upload.comp.spv").c_str(), g_RenderState.SparseUploadPipeline, g_RenderState.SparseUploadLayout);
}

internal vk::util::material*
GetMaterial(const std::string& name)
{
  auto it = g_RenderState.Materials.find(name);
  if (it == g_RenderState.Materials.end())
  {
    return nullptr;
  }
  else
  {
    return &(*it).second;
  }
}

internal Mesh*
GetMesh(const std::string& name)
{
  auto it = g_RenderState.Meshes.find(name);
  if (it == g_RenderState.Meshes.end())
  {
    return nullptr;
  }
  else
  {
    return &(*it).second;
  }
}

internal void
UploadMesh(Mesh& mesh)
{
  const size_t bufferSize = mesh.Vertices.size() * sizeof(Vertex);
  VkBufferCreateInfo stagingBufferInfo = {};
  stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingBufferInfo.pNext = nullptr;
  stagingBufferInfo.size = bufferSize;  
  stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  VmaAllocationCreateInfo vmaAllocInfo = {};
  vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  allocated_buffer_untyped stagingBuffer;

  VK_CHECK(vmaCreateBuffer(g_Allocator, &stagingBufferInfo, &vmaAllocInfo,
    &stagingBuffer.Buffer,
    &stagingBuffer.Allocation,
    nullptr));

  void* data;
  vmaMapMemory(g_Allocator, stagingBuffer.Allocation, &data);

  memcpy(data, mesh.Vertices.data(), mesh.Vertices.size() * sizeof(Vertex));

  vmaUnmapMemory(g_Allocator, stagingBuffer.Allocation);

  VkBufferCreateInfo vertexBufferInfo = {};
  vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertexBufferInfo.pNext = nullptr;
  vertexBufferInfo.size = bufferSize;
  vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VK_CHECK(vmaCreateBuffer(g_Allocator, &vertexBufferInfo, &vmaAllocInfo,
    &mesh.VertexBuffer.Buffer,
    &mesh.VertexBuffer.Allocation,
    nullptr));

  ImmediateSubmit([=](VkCommandBuffer cmd)
  {
    VkBufferCopy copy;
    copy.dstOffset = 0;
    copy.srcOffset = 0;
    copy.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.Buffer, mesh.VertexBuffer.Buffer, 1, &copy);
  });

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vmaDestroyBuffer(g_Allocator, mesh.VertexBuffer.Buffer, mesh.VertexBuffer.Allocation);
  });

  vmaDestroyBuffer(g_Allocator, stagingBuffer.Buffer, stagingBuffer.Allocation);
}

internal void
LoadMeshes()
{
  Mesh ObjMesh;

  ObjMesh.Bounds.Valid = false;

  ObjMesh.LoadFromObj("data/models/Spaceship.obj");

  UploadMesh(ObjMesh);

  g_RenderState.Meshes["ship"] = ObjMesh;
}

bool
LoadImageToCache(const char* name, const char* path)
{
	// ZoneScopedNC("Load Texture", tracy::Color::Yellow);
	Texture newtex;

	if (g_RenderState.LoadedTextures.find(name) != g_RenderState.LoadedTextures.end()) return true;

	// bool result = vk::util::LoadImageFromAsset(*this, path, newtex.image);

  newtex.Image = vk::util::load_image_from_asset(path);
  
  VkImageViewCreateInfo imageinfo = vk::init::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, newtex.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT);
  vkCreateImageView(g_PlatformState.Device, &imageinfo, nullptr, &newtex.ImageView);

  g_RenderState.LoadedTextures[name] = newtex;

	// if (!result)
	// {
	// 	// LOG_ERROR("Error When texture {} at path {}", name, path);
	// 	return false;
	// }
	// else
  // {
	// 	// LOG_SUCCESS("Loaded texture {} at path {}", name, path);
	// }

	newtex.ImageView = newtex.Image.DefaultView;
	//VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, newtex.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	//imageinfo.subresourceRange.levelCount = newtex.image.mipLevels;
	//vkCreateImageView(_device, &imageinfo, nullptr, &newtex.imageView);

	g_RenderState.LoadedTextures[name] = newtex;

	return true;
}

internal void
LoadImages()
{
  // Texture boxColor = {};
  
  // // boxColor.Image = vk::util::load_image_from_file("data/models/Spaceship_color.jpg");
  // boxColor.Image = vk::util::load_image_from_asset("data/models/assets_export/Spaceship_color.tx");
  
  // VkImageViewCreateInfo imageinfo = vk::init::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, boxColor.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT);
  // vkCreateImageView(g_PlatformState.Device, &imageinfo, nullptr, &boxColor.ImageView);

  // g_RenderState.LoadedTextures["box_color"] = boxColor;

  LoadImageToCache("box_color", "data/models/assets_export/Spaceship_color.tx");
}

bool
LoadPrefab(const char* path, glm::mat4 root)
{
	int rng = rand();

	VkSamplerCreateInfo samplerInfo = vk::init::sampler_create_info(VK_FILTER_LINEAR);
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	VkSampler smoothSampler;
	vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &smoothSampler);
 

	std::vector<MeshObject> prefab_renderables;

	prefab_renderables.reserve(1);

	
		
    //########################################################################

  if (!GetMesh("ship"))
  {
    Mesh mesh{};
    // mesh.load_from_meshasset(asset_path(v.mesh_path).c_str());
    mesh.LoadFromObj("data/models/Spaceship.obj");

    UploadMesh(mesh);

    g_RenderState.Meshes["ship"] = mesh;
  }

  auto materialName = "mat"; // v.material_path.c_str();
                             //load material

  vk::util::material *objectMaterial = g_RenderState.MaterialSystem->GetMaterial(materialName);

  if (!objectMaterial)
  {
    vk::util::sampled_texture tex;

    tex.View = g_RenderState.LoadedTextures["box_color"].ImageView;

    tex.Sampler = g_RenderState.SmoothSampler;

    vk::util::material_data info;
    info.Parameters = nullptr;

    // if (material.Transparency == assets::TransparencyMode::Transparent)
    // {
    //   info.baseTemplate = "texturedPBR_transparent";
    // }
    // else
    {
      info.BaseTemplate = "texturedPBR_opaque";
    }

    info.Textures.push_back(tex);

    objectMaterial = g_RenderState.MaterialSystem->BuildMaterial(materialName, info, *g_RenderState.DescriptorLayoutCache, *g_RenderState.DescriptorAllocator);

    if (!objectMaterial)
    {
      logger::err("Error When building material");
      // LOG_ERROR("Error When building material {}", v.material_path);
    }
  }

  //########################################################################

  MeshObject loadmesh;
  //transparent objects will be invisible

  loadmesh.bDrawForwardPass = true;
  loadmesh.bDrawShadowPass = true;

  //glm::mat4 nodematrix{1.f};

  // auto matrixIT = node_worldmats.find(k);
  // if (matrixIT != node_worldmats.end())
  // {
  //   auto nm = (*matrixIT).second;
  //   memcpy(&nodematrix, &nm, sizeof(glm::mat4));
  // }

  loadmesh.Mesh = GetMesh("ship");
  loadmesh.TransformMatrix = root;//nodematrix;
  loadmesh.Material = objectMaterial;

  // RefreshRenderbounds(&loadmesh);

  //sort key from location
  // int32_t lx = int(loadmesh.Bounds.origin.x / 10.f);
  // int32_t ly = int(loadmesh.Bounds.origin.y / 10.f);

  //uint32_t key = uint32_t(std::hash<int32_t>()(lx) ^ std::hash<int32_t>()(ly ^ 1337));

  // loadmesh.customSortKey = 0; // rng;// key;

  prefab_renderables.push_back(loadmesh);
  //_renderables.push_back(loadmesh);

  g_RenderState.RenderScene->RegisterObjectBatch(prefab_renderables.data(), static_cast<u32>(prefab_renderables.size()));

	return true;
}

internal void
InitScene()
{
  // RenderObject ship;
  // ship.Mesh = GetMesh("ship");
  // ship.Material = GetMaterial("texturedmesh");
  // ship.TransformMatrix = glm::mat4{ 1.0f };

  // g_RenderState.Renderables.push_back(ship);

  // Material* texturedMat =  GetMaterial("texturedmesh");

  // VkDescriptorSetAllocateInfo allocInfo = {};
  // allocInfo.pNext = nullptr;
  // allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  // allocInfo.descriptorPool = g_RenderState.DescriptorPool;
  // allocInfo.descriptorSetCount = 1;
  // allocInfo.pSetLayouts = &g_RenderState.SingleTextureSetLayout;

  // vkAllocateDescriptorSets(g_PlatformState.Device, &allocInfo, &texturedMat->TextureSet);

  // VkSamplerCreateInfo samplerInfo = vk::init::sampler_create_info(VK_FILTER_NEAREST);

  // VkSampler blockySampler;
  // vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &blockySampler);

  // g_RenderState.MainDeletionQueue.push_function([=]()
  // {
  //   vkDestroySampler(g_PlatformState.Device, blockySampler, nullptr);
  // });

  // VkDescriptorImageInfo imageBufferInfo;
  // imageBufferInfo.sampler = blockySampler;
  // imageBufferInfo.imageView = g_RenderState.LoadedTextures["box_color"].ImageView;
  // imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // VkWriteDescriptorSet texture1 = vk::init::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMat->TextureSet, &imageBufferInfo, 0);

  // vkUpdateDescriptorSets(g_PlatformState.Device, 1, &texture1, 0, nullptr);

  VkSamplerCreateInfo samplerInfo = vk::init::sampler_create_info(VK_FILTER_NEAREST);

  VkSampler blockySampler;
	vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &blockySampler);

  samplerInfo = vk::init::sampler_create_info(VK_FILTER_LINEAR);

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//info.anisotropyEnable = true;
	samplerInfo.mipLodBias = 2;
	samplerInfo.maxLod = 30.f;
	samplerInfo.minLod = 3;
	VkSampler smoothSampler;

	vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &smoothSampler);

  {
		vk::util::material_data texturedInfo;
		texturedInfo.BaseTemplate = "texturedPBR_opaque";
		texturedInfo.Parameters = nullptr;

		vk::util::sampled_texture whiteTex;
		whiteTex.Sampler = smoothSampler;
		whiteTex.View = g_RenderState.LoadedTextures["box_color"].ImageView;

		texturedInfo.Textures.push_back(whiteTex);

		vk::util::material* newmat = g_RenderState.MaterialSystem->BuildMaterial("textured", texturedInfo, *g_RenderState.DescriptorLayoutCache, *g_RenderState.DescriptorAllocator);
	}
	{
		vk::util::material_data matinfo;
		matinfo.BaseTemplate = "texturedPBR_opaque";
		matinfo.Parameters = nullptr;
	
		vk::util::sampled_texture whiteTex;
		whiteTex.Sampler = smoothSampler;
		whiteTex.View = g_RenderState.LoadedTextures["box_color"].ImageView;

		matinfo.Textures.push_back(whiteTex);

		vk::util::material* newmat = g_RenderState.MaterialSystem->BuildMaterial("default", matinfo, *g_RenderState.DescriptorLayoutCache, *g_RenderState.DescriptorAllocator);
	}

  int dimHelmets = 1;
	for (int x = -dimHelmets; x <= dimHelmets; x++)
  {
		for (int y = -dimHelmets; y <= dimHelmets; y++)
    {
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x * 5, 10, y * 5));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(10));
	
			//load_prefab(asset_path("FlightHelmet/FlightHelmet.pfb").c_str(),(translation * scale));
      LoadPrefab("", (translation * scale));
		}
	}

	//glm::mat4 sponzaMatrix = glm::scale(glm::mat4{ 1.0 }, glm::vec3(1));;
	
	//glm::mat4 unrealFixRotation = glm::rotate(glm::radians(-90.f), glm::vec3{ 1,0,0 });


}

internal void
InitImGui()
{
  VkDescriptorPoolSize pool_sizes[] =
  {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = (u32)std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imguiPool;
  VK_CHECK(vkCreateDescriptorPool(g_PlatformState.Device, &pool_info, nullptr, &imguiPool));
  
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = NULL;

  ImGui::StyleColorsDark();

  ImGui_ImplWin32_Init(g_PlatformState.Win32NativeWindow);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = g_PlatformState.Instance;
  init_info.PhysicalDevice = g_PlatformState.ChosenGPU;
  init_info.Device = g_PlatformState.Device;
  init_info.Queue = g_RenderState.GraphicsQueue;
  init_info.QueueFamily = g_RenderState.GraphicsQueueFamily;
  init_info.DescriptorPool = imguiPool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;

  ImGui_ImplVulkan_Init(&init_info, g_RenderState.RenderPass);

  ImmediateSubmit([&](VkCommandBuffer cmd)
  {
    ImGui_ImplVulkan_CreateFontsTexture(cmd);
  });

  ImGui_ImplVulkan_DestroyFontUploadObjects();

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyDescriptorPool(g_PlatformState.Device, imguiPool, nullptr);
    ImGui_ImplVulkan_Shutdown();
  });
}

internal void
Init()
{
  InitSwapchain();
  InitCommands();
  InitSyncStructures();
  InitDescriptors();
  //InitDefaultRenderpass();
  InitFramebuffers();
  InitPipelines();

  LoadImages();
  LoadMeshes();
  InitScene();

  InitImGui();
}

static render_scene g_RenderScene;
static vk::shader::shader_cache g_ShaderCache;

internal void
Init2()
{

  g_RenderState.Meshes.reserve(1000);

  g_RenderState.RenderScene = &g_RenderScene;

  g_RenderState.ShaderCache = &g_ShaderCache;

  g_RenderState.RenderScene->Init();

  InitSwapchain(); // +

  InitForwardRenderpass(); // +/-
	InitCopyRenderpass(); // +/-
	InitShadowRenderpass(); // +/-

  InitFramebuffers(); // +/-

  InitCommands(); // +

  InitSyncStructures(); // +

  InitDescriptors(); // +

  //InitDefaultRenderpass();

  InitPipelines(); // +

  //

  LoadImages(); // +

  LoadMeshes(); // +

  InitScene(); // +

  InitImGui(); // +?

  g_RenderState.RenderScene->BuildBatches();

	g_RenderState.RenderScene->MergeMeshes();

	//everything went fine
	g_RenderState.IsInitialized = true;

	g_RenderState.Camera = {};
	g_RenderState.Camera.Position = { 0.f,6.f,5.f };

	g_RenderState.MainLight.lightPosition = { 0,0,0 };
	g_RenderState.MainLight.lightDirection = glm::vec3(0.3, -1, 0.3);
	g_RenderState.MainLight.shadowExtent = { 100 ,100 ,100 };
}

internal void
Resize()
{
  vkDeviceWaitIdle(g_PlatformState.Device);

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PlatformState.ChosenGPU, g_PlatformState.Surface, &surfaceCapabilities);

  if(g_PlatformState.WindowExtent.width == surfaceCapabilities.currentExtent.width &&
     g_PlatformState.WindowExtent.height == surfaceCapabilities.currentExtent.height) return;

  g_PlatformState.WindowExtent = surfaceCapabilities.currentExtent;

  g_RenderState.MainDeletionQueue.flush();

  vkDeviceWaitIdle(g_PlatformState.Device);

  Init();
}

internal void
Cleanup()
{
  // vkDeviceWaitIdle(g_PlatformState.Device);

  // g_RenderState.MainDeletionQueue.flush();

  // vkDeviceWaitIdle(g_PlatformState.Device);

  // vkDestroySurfaceKHR(g_PlatformState.Instance, g_PlatformState.Surface, nullptr);

  // vmaDestroyAllocator(g_Allocator);

  // vkDestroyDevice(g_PlatformState.Device, nullptr);

  // vkb::destroy_debug_utils_messenger(g_PlatformState.Instance, g_PlatformState.DebugMessenger);

  // vkDestroyInstance(g_PlatformState.Instance, nullptr);

  if (g_RenderState.IsInitialized)
  {

		//make sure the gpu has stopped doing its things
		for (auto& frame : g_RenderState.Frames)
		{
			vkWaitForFences(g_PlatformState.Device, 1, &frame._renderFence, true, 1000000000);
		}

		g_RenderState.MainDeletionQueue.flush();

		for (auto& frame : g_RenderState.Frames)
		{
			frame.DynamicDescriptorAllocator->Cleanup();
		}

		g_RenderState.DescriptorAllocator->Cleanup();
		g_RenderState.DescriptorLayoutCache->Cleanup();

		vkDestroySurfaceKHR(g_PlatformState.Instance, g_PlatformState.Surface, nullptr);

    vmaDestroyAllocator(g_Allocator);

		vkDestroyDevice(g_PlatformState.Device, nullptr);

    vkb::destroy_debug_utils_messenger(g_PlatformState.Instance, g_PlatformState.DebugMessenger);

		vkDestroyInstance(g_PlatformState.Instance, nullptr);
	}
}

}