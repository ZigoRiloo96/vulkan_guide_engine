

#include "vk_renderer.h"

namespace vk::renderer
{
  global_variable vulkan_platform_state g_PlatformState {};
  global_variable vulkan_render_state g_RenderState {};
  global_variable pipeline_builder g_PipelineBuilder {};

  static VkAllocationCallbacks* g_VkAllocator = NULL;
  global_variable VmaAllocator g_Allocator;

  allocated_buffer_untyped
  CreateBuffer(u64 allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlags = 0);

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

allocated_buffer_untyped
CreateBuffer(u64 allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkMemoryPropertyFlags requiredFlags)
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
  Material* Material;
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
Win32InitVulkan(HWND WindowWin32, HINSTANCE InstanceWin32)
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

  VkExtent3D depthImageExtent =
  {
    g_PlatformState.WindowExtent.width,
    g_PlatformState.WindowExtent.height,
    1
  };

  g_RenderState.DepthFormat = VK_FORMAT_D32_SFLOAT;

  VkImageCreateInfo dimg_info = vk::init::image_create_info(g_RenderState.DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

  VmaAllocationCreateInfo dimg_allocinfo = {};
  dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vmaCreateImage(g_Allocator, &dimg_info, &dimg_allocinfo, &g_RenderState.DepthImage.Image, &g_RenderState.DepthImage.Allocation, nullptr);

  VkImageViewCreateInfo dview_info = vk::init::imageview_create_info(g_RenderState.DepthFormat, g_RenderState.DepthImage.Image, VK_IMAGE_ASPECT_DEPTH_BIT);

  VK_CHECK(vkCreateImageView(g_PlatformState.Device, &dview_info, nullptr, &g_RenderState.DepthImageView));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyImageView(g_PlatformState.Device, g_RenderState.DepthImageView, nullptr);
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

internal void
InitDefaultRenderpass()
{
  VkAttachmentDescription color_attachment = {};
  color_attachment.format = g_RenderState.SwapchainImageFormat;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref = {};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription depth_attachment = {};
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

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;
  subpass.pDepthStencilAttachment = &depth_attachment_ref;

  VkAttachmentDescription attachments[2] = { color_attachment, depth_attachment };

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 2;
  render_pass_info.pAttachments = &attachments[0];
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  
  VK_CHECK(vkCreateRenderPass(g_PlatformState.Device, &render_pass_info, nullptr, &g_RenderState.RenderPass));

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyRenderPass(g_PlatformState.Device, g_RenderState.RenderPass, nullptr);
  });
}

internal void
InitFramebuffers()
{
  VkFramebufferCreateInfo fb_info = vk::init::framebuffer_create_info( g_RenderState.RenderPass, g_PlatformState.WindowExtent );

  const u64 framebufferSize = g_RenderState.SwapchainImages.size();
  g_RenderState.Framebuffers.resize(framebufferSize);
  
  for (u64 i = 0; i < framebufferSize; i++)
  {
    VkImageView attachments[2];
    attachments[0] = g_RenderState.SwapchainImageViews[i];
    attachments[1] = g_RenderState.DepthImageView;

    fb_info.pAttachments = attachments;
    fb_info.attachmentCount = 2;

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
  std::vector<VkDescriptorPoolSize> sizes =
  {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 }
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = 0;
  pool_info.maxSets = 10;
  pool_info.poolSizeCount = (u32)sizes.size();
  pool_info.pPoolSizes = sizes.data();
  
  vkCreateDescriptorPool(g_PlatformState.Device, &pool_info, nullptr, &g_RenderState.DescriptorPool);

  VkDescriptorSetLayoutBinding cameraBind = vk::init::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0);
  VkDescriptorSetLayoutBinding sceneBind = vk::init::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
  
  VkDescriptorSetLayoutBinding bindings[] = { cameraBind, sceneBind };  
  
  VkDescriptorSetLayoutCreateInfo setInfo = {};
  setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setInfo.pNext = nullptr;
  setInfo.bindingCount = 2;
  setInfo.flags = 0;
  setInfo.pBindings = bindings;

  vkCreateDescriptorSetLayout(g_PlatformState.Device, &setInfo, nullptr, &g_RenderState.GlobalSetLayout);

  VkDescriptorSetLayoutBinding objectBind = vk::init::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);

  VkDescriptorSetLayoutCreateInfo set2info = {};
  set2info.bindingCount = 1;
  set2info.flags = 0;
  set2info.pNext = nullptr;
  set2info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  set2info.pBindings = &objectBind;

  vkCreateDescriptorSetLayout(g_PlatformState.Device, &set2info, nullptr, &g_RenderState.ObjectSetLayout);

  VkDescriptorSetLayoutBinding textureBind = vk::init::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

  VkDescriptorSetLayoutCreateInfo set3info = {};
  set3info.bindingCount = 1;
  set3info.flags = 0;
  set3info.pNext = nullptr;
  set3info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  set3info.pBindings = &textureBind;

  vkCreateDescriptorSetLayout(g_PlatformState.Device, &set3info, nullptr, &g_RenderState.SingleTextureSetLayout);

  const u64 sceneParamBufferSize = FRAME_OVERLAP * PadUniformBufferSize(sizeof(GPUSceneData));
  g_RenderState.SceneParameterBuffer = CreateBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

  for (int i = 0; i < FRAME_OVERLAP; i++)
  {
    g_RenderState.Frames[i].CameraBuffer = CreateBuffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    const int MAX_OBJECTS = 10000;
    g_RenderState.Frames[i].ObjectBuffer = CreateBuffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.pNext = nullptr;
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_RenderState.DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &g_RenderState.GlobalSetLayout;

    vkAllocateDescriptorSets(g_PlatformState.Device, &allocInfo, &g_RenderState.Frames[i].GlobalDescriptor);

    VkDescriptorSetAllocateInfo objectSetAlloc = {};
    objectSetAlloc.pNext = nullptr;
    objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    objectSetAlloc.descriptorPool = g_RenderState.DescriptorPool;
    objectSetAlloc.descriptorSetCount = 1;
    objectSetAlloc.pSetLayouts = &g_RenderState.ObjectSetLayout;

    vkAllocateDescriptorSets(g_PlatformState.Device, &objectSetAlloc, &g_RenderState.Frames[i].ObjectDescriptor);

    //

    VkDescriptorBufferInfo cameraInfo = {};
    cameraInfo.buffer = g_RenderState.Frames[i].CameraBuffer.Buffer;
    cameraInfo.offset = 0;
    cameraInfo.range = sizeof(GPUCameraData);

    VkDescriptorBufferInfo sceneInfo = {};
    sceneInfo.buffer = g_RenderState.SceneParameterBuffer.Buffer;
    sceneInfo.offset = PadUniformBufferSize(sizeof(GPUSceneData)) * i;
    sceneInfo.range = sizeof(GPUSceneData);

    VkDescriptorBufferInfo objectBufferInfo;
    objectBufferInfo.buffer = g_RenderState.Frames[i].ObjectBuffer.Buffer;
    objectBufferInfo.offset = 0;
    objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

    VkWriteDescriptorSet cameraWrite = vk::init::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, g_RenderState.Frames[i].GlobalDescriptor, &cameraInfo, 0);
    VkWriteDescriptorSet sceneWrite = vk::init::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, g_RenderState.Frames[i].GlobalDescriptor, &sceneInfo, 1);
    VkWriteDescriptorSet objectWrite = vk::init::write_descriptor_buffer(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, g_RenderState.Frames[i].ObjectDescriptor, &objectBufferInfo, 0);

    VkWriteDescriptorSet setWrites[] = { cameraWrite, sceneWrite, objectWrite };
     

    vkUpdateDescriptorSets(g_PlatformState.Device, 3, setWrites, 0, nullptr);    
  }
}

internal void
DrawObjects(VkCommandBuffer cmd, RenderObject* first, u64 count)
{
  FrameData& currentFrame = g_RenderState.GetCurrentFrame();

  glm::vec3 camPos = { 0.f,-2.f,-10.f };

  glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
  glm::mat4 projection = glm::perspective(glm::radians(70.f), (f32)g_PlatformState.WindowExtent.width / g_PlatformState.WindowExtent.height, 0.1f, 200.0f);//1700.f / 900.f, 0.1f, 200.0f);//g_PlatformState.WindowExtent
  projection[1][1] *= -1;

  GPUCameraData camData;
  camData.Projection = projection;
  camData.View = view;
  camData.Viewproj = projection * view;

  void* data;
  vmaMapMemory(g_Allocator, currentFrame.CameraBuffer.Allocation, &data);

  memcpy(data, &camData, sizeof(GPUCameraData));

  vmaUnmapMemory(g_Allocator, currentFrame.CameraBuffer.Allocation);

  float framed = (g_RenderState.FrameNumber / 120.f);

  g_RenderState.SceneParameters.AmbientColor = { sin(framed),0,cos(framed),1 };

  char* sceneData;
  vmaMapMemory(g_Allocator, g_RenderState.SceneParameterBuffer.Allocation , (void**)&sceneData);

  int frameIndex = g_RenderState.FrameNumber % FRAME_OVERLAP;

  sceneData += PadUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;

  memcpy(sceneData, &g_RenderState.SceneParameters, sizeof(GPUSceneData));

  vmaUnmapMemory(g_Allocator, g_RenderState.SceneParameterBuffer.Allocation);

  void* objectData;

  vmaMapMemory(g_Allocator, currentFrame.ObjectBuffer.Allocation, &objectData);

  GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

  for (int i = 0; i < count; i++)
  {
    RenderObject& object = first[i];
    objectSSBO[i].ModelMatrix = object.TransformMatrix;
  }

  vmaUnmapMemory(g_Allocator, currentFrame.ObjectBuffer.Allocation);

  Mesh* lastMesh = nullptr;
  Material* lastMaterial = nullptr;

  std::vector<indirect_batch> draws = CompactDraws(first, (int)count);

  for(indirect_batch& draw : draws)
  {
    if (draw.Material != lastMaterial)
    {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->Pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 0, 1, &currentFrame.GlobalDescriptor, 0, nullptr);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 1, 1, &currentFrame.ObjectDescriptor, 0, nullptr);
      if (draw.Material->TextureSet != VK_NULL_HANDLE)
      {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Material->PipelineLayout, 2, 1, &draw.Material->TextureSet, 0, nullptr);
      }

      lastMaterial = draw.Material;
    }

    //MeshPushConstants constants;
    //constants.RenderMatrix = draw.TransformMatrix;
    //vkCmdPushConstants(cmd, draw.Material->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

    if (draw.Mesh != lastMesh)
    {
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &draw.Mesh->VertexBuffer.Buffer, &offset);
      lastMesh = draw.Mesh;
    }

    for(u32 i = draw.First; i < draw.Count; i++)
    {
      vkCmdDraw(cmd, (u32)draw.Mesh->Vertices.size(), 1, 0, (u32)i);
    }
  }

  // for (u64 i = 0; i < count; i++)
  // {
  //   RenderObject& object = first[i];

  //   if (object.Material != lastMaterial)
  //   {
  //     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->Pipeline);
  //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 0, 1, &currentFrame.GlobalDescriptor, 0, nullptr);
  //     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 1, 1, &currentFrame.ObjectDescriptor, 0, nullptr);
  //     if (object.Material->TextureSet != VK_NULL_HANDLE)
  //     {
  //       vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.Material->PipelineLayout, 2, 1, &object.Material->TextureSet, 0, nullptr);
  //     }

  //     lastMaterial = object.Material;
  //   }

  //   MeshPushConstants constants;
  //   constants.RenderMatrix = object.TransformMatrix;
  //   vkCmdPushConstants(cmd, object.Material->PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

  //   if (object.Mesh != lastMesh)
  //   {
  //     VkDeviceSize offset = 0;
  //     vkCmdBindVertexBuffers(cmd, 0, 1, &object.Mesh->VertexBuffer.Buffer, &offset);
  //     lastMesh = object.Mesh;
  //   }
    
  //   vkCmdDraw(cmd, (u32)object.Mesh->Vertices.size(), 1, 0, (u32)i);
  // }
}

void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
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

internal void
Draw()
{
  FrameData& currentFrame = g_RenderState.GetCurrentFrame();

  VK_CHECK(vkWaitForFences(g_PlatformState.Device, 1, &currentFrame.RenderFence, true, 1000000000));
  VK_CHECK(vkResetFences(g_PlatformState.Device, 1, &currentFrame.RenderFence));

  u32 swapchainImageIndex;
  VK_CHECK(vkAcquireNextImageKHR(g_PlatformState.Device, g_RenderState.Swapchain, 1000000000, currentFrame.PresentSemaphore, nullptr, &swapchainImageIndex));

  VK_CHECK(vkResetCommandBuffer(currentFrame.MainCommandBuffer, 0));

  VkCommandBuffer cmd = currentFrame.MainCommandBuffer;

  VkCommandBufferBeginInfo cmdBeginInfo = {};
  cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cmdBeginInfo.pNext = nullptr;
  cmdBeginInfo.pInheritanceInfo = nullptr;
  cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

  VkClearValue clearValue;
  float flash = abs(sin(g_RenderState.FrameNumber / 120.f));
  clearValue.color = { { 0.0f, 0.0f, flash * 0.5f, 1.0f } };

  VkClearValue depthClear;
  depthClear.depthStencil.depth = 1.f;

  VkRenderPassBeginInfo rpInfo = vk::init::renderpass_begin_info(g_RenderState.RenderPass, g_PlatformState.WindowExtent, g_RenderState.Framebuffers[swapchainImageIndex]);

  rpInfo.clearValueCount = 2;

  VkClearValue clearValues[] = { clearValue, depthClear };

  rpInfo.pClearValues = &clearValues[0];

  vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

  DrawObjects(cmd, g_RenderState.Renderables.data(), g_RenderState.Renderables.size());

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  vkCmdEndRenderPass(cmd);
  VK_CHECK(vkEndCommandBuffer(cmd));

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.pNext = nullptr;

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  submit.pWaitDstStageMask = &waitStage;

  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &currentFrame.PresentSemaphore;

  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &currentFrame.RenderSemaphore;

  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  VK_CHECK(vkQueueSubmit(g_RenderState.GraphicsQueue, 1, &submit, currentFrame.RenderFence));

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.pNext = nullptr;

  presentInfo.pSwapchains = &g_RenderState.Swapchain;
  presentInfo.swapchainCount = 1;

  presentInfo.pWaitSemaphores = &currentFrame.RenderSemaphore;
  presentInfo.waitSemaphoreCount = 1;

  presentInfo.pImageIndices = &swapchainImageIndex;

  VK_CHECK(vkQueuePresentKHR(g_RenderState.GraphicsQueue, &presentInfo));

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

internal Material*
CreateMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
  Material mat;
  mat.Pipeline = pipeline;
  mat.PipelineLayout = layout;
  g_RenderState.Materials[name] = mat;
  return &g_RenderState.Materials[name];
}

internal void
InitPipelines()
{
  VkShaderModule triangleFragShader = LoadShaderModule("data/shaders/colored_triangle.frag.spv");
  VkShaderModule triangleVertexShader = LoadShaderModule("data/shaders/colored_triangle.vert.spv");

  VkShaderModule redTriangleFragShader = LoadShaderModule("data/shaders/triangle.frag.spv");
  VkShaderModule redTriangleVertexShader = LoadShaderModule("data/shaders/triangle.vert.spv");

  VkShaderModule meshVertShader = LoadShaderModule("data/shaders/tri_mesh.vert.spv");
  VkShaderModule colorMeshFragShader = LoadShaderModule("data/shaders/default_lit.frag.spv");

  VkShaderModule texturedMeshFragShader = LoadShaderModule("data/shaders/textured_lit.frag.spv");

  VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vk::init::pipeline_layout_create_info();
  
  VkPushConstantRange push_constant;
  push_constant.offset = 0;
  push_constant.size = sizeof(MeshPushConstants);
  push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
  mesh_pipeline_layout_info.pushConstantRangeCount = 1;

  VkDescriptorSetLayout setLayouts[] = { g_RenderState.GlobalSetLayout, g_RenderState.ObjectSetLayout };

  mesh_pipeline_layout_info.setLayoutCount = 2;
  mesh_pipeline_layout_info.pSetLayouts = setLayouts;

  VK_CHECK(vkCreatePipelineLayout(g_PlatformState.Device, &mesh_pipeline_layout_info, nullptr, &g_RenderState.MeshPipelineLayout));

  VkPipelineLayoutCreateInfo textured_pipeline_layout_info = mesh_pipeline_layout_info;
    
  VkDescriptorSetLayout texturedSetLayouts[] = { g_RenderState.GlobalSetLayout, g_RenderState.ObjectSetLayout, g_RenderState.SingleTextureSetLayout };

  textured_pipeline_layout_info.setLayoutCount = 3;
  textured_pipeline_layout_info.pSetLayouts = texturedSetLayouts;

  VkPipelineLayout texturedPipeLayout;
  VK_CHECK(vkCreatePipelineLayout(g_PlatformState.Device, &textured_pipeline_layout_info, nullptr, &texturedPipeLayout));

  g_PipelineBuilder.ShaderStages.clear();
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

  g_PipelineBuilder.VertexInputInfo = vk::init::vertex_input_state_create_info();
  
  g_PipelineBuilder.InputAssembly = vk::init::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  g_PipelineBuilder.Viewport.x = 0.0f;
  g_PipelineBuilder.Viewport.y = 0.0f;
  g_PipelineBuilder.Viewport.width = (float)g_PlatformState.WindowExtent.width;
  g_PipelineBuilder.Viewport.height = (float)g_PlatformState.WindowExtent.height;
  g_PipelineBuilder.Viewport.minDepth = 0.0f;
  g_PipelineBuilder.Viewport.maxDepth = 1.0f;
  
  g_PipelineBuilder.Scissor.offset = { 0, 0 };
  g_PipelineBuilder.Scissor.extent = g_PlatformState.WindowExtent;

  g_PipelineBuilder.Rasterizer = vk::init::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
  g_PipelineBuilder.Multisampling = vk::init::multisampling_state_create_info();
  g_PipelineBuilder.ColorBlendAttachment = vk::init::color_blend_attachment_state();
  g_PipelineBuilder.PipelineLayout = g_RenderState.MeshPipelineLayout;
  g_PipelineBuilder.DepthStencil = vk::init::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

  g_RenderState.TrianglePipeline = BuildPipeline(g_PlatformState.Device, g_RenderState.RenderPass);

  g_PipelineBuilder.ShaderStages.clear();
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, redTriangleVertexShader));
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, redTriangleFragShader));

  g_RenderState.RedTrianglePipeline = BuildPipeline(g_PlatformState.Device, g_RenderState.RenderPass);
  
  //

  VertexInputDescription vertexDescription = Vertex::get_vertex_description();

  g_PipelineBuilder.VertexInputInfo.pVertexAttributeDescriptions = vertexDescription.Attributes.data();
  g_PipelineBuilder.VertexInputInfo.vertexAttributeDescriptionCount = (u32)vertexDescription.Attributes.size();

  g_PipelineBuilder.VertexInputInfo.pVertexBindingDescriptions = vertexDescription.Bindings.data();
  g_PipelineBuilder.VertexInputInfo.vertexBindingDescriptionCount = (u32)vertexDescription.Bindings.size();

  g_PipelineBuilder.ShaderStages.clear();
    
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, colorMeshFragShader));

  g_RenderState.MeshPipeline = BuildPipeline(g_PlatformState.Device, g_RenderState.RenderPass);

  CreateMaterial(g_RenderState.MeshPipeline, g_RenderState.MeshPipelineLayout, "defaultmesh");

  g_PipelineBuilder.ShaderStages.clear();
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));
  g_PipelineBuilder.ShaderStages.push_back(vk::init::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshFragShader));

  g_PipelineBuilder.PipelineLayout = texturedPipeLayout;
  VkPipeline texPipeline = BuildPipeline(g_PlatformState.Device, g_RenderState.RenderPass);
  CreateMaterial(texPipeline, texturedPipeLayout, "texturedmesh");

  vkDestroyShaderModule(g_PlatformState.Device, meshVertShader, nullptr);
  vkDestroyShaderModule(g_PlatformState.Device, redTriangleVertexShader, nullptr);
  vkDestroyShaderModule(g_PlatformState.Device, redTriangleFragShader, nullptr);
  vkDestroyShaderModule(g_PlatformState.Device, triangleFragShader, nullptr);
  vkDestroyShaderModule(g_PlatformState.Device, triangleVertexShader, nullptr);
  vkDestroyShaderModule(g_PlatformState.Device, colorMeshFragShader, nullptr);

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroyPipeline(g_PlatformState.Device, g_RenderState.RedTrianglePipeline, nullptr);
    vkDestroyPipeline(g_PlatformState.Device, g_RenderState.TrianglePipeline, nullptr);
    vkDestroyPipeline(g_PlatformState.Device, g_RenderState.MeshPipeline, nullptr);

    vkDestroyPipelineLayout(g_PlatformState.Device, g_RenderState.MeshPipelineLayout, nullptr);
  });
}

internal Material*
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
  ObjMesh.LoadFromObj("data/models/Spaceship.obj");

  UploadMesh(ObjMesh);

  g_RenderState.Meshes["ship"] = ObjMesh;
}

internal void
LoadImages()
{
  Texture boxColor = {};
  
  // boxColor.Image = vk::util::load_image_from_file("data/models/Spaceship_color.jpg");
  boxColor.Image = vk::util::load_image_from_asset("data/models/assets_export/Spaceship_color.tx");
  
  VkImageViewCreateInfo imageinfo = vk::init::imageview_create_info(VK_FORMAT_R8G8B8A8_UNORM, boxColor.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT);
  vkCreateImageView(g_PlatformState.Device, &imageinfo, nullptr, &boxColor.ImageView);

  g_RenderState.LoadedTextures["box_color"] = boxColor;
}

internal void
InitScene()
{
  RenderObject ship;
  ship.Mesh = GetMesh("ship");
  ship.Material = GetMaterial("texturedmesh");
  ship.TransformMatrix = glm::mat4{ 1.0f };

  g_RenderState.Renderables.push_back(ship);

  Material* texturedMat =  GetMaterial("texturedmesh");

  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.pNext = nullptr;
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = g_RenderState.DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &g_RenderState.SingleTextureSetLayout;

  vkAllocateDescriptorSets(g_PlatformState.Device, &allocInfo, &texturedMat->TextureSet);

  VkSamplerCreateInfo samplerInfo = vk::init::sampler_create_info(VK_FILTER_NEAREST);

  VkSampler blockySampler;
  vkCreateSampler(g_PlatformState.Device, &samplerInfo, nullptr, &blockySampler);

  g_RenderState.MainDeletionQueue.push_function([=]()
  {
    vkDestroySampler(g_PlatformState.Device, blockySampler, nullptr);
  });

  VkDescriptorImageInfo imageBufferInfo;
  imageBufferInfo.sampler = blockySampler;
  imageBufferInfo.imageView = g_RenderState.LoadedTextures["box_color"].ImageView;
  imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet texture1 = vk::init::write_descriptor_image(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMat->TextureSet, &imageBufferInfo, 0);

  vkUpdateDescriptorSets(g_PlatformState.Device, 1, &texture1, 0, nullptr);
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
  InitDefaultRenderpass();
  InitFramebuffers();
  InitPipelines();

  LoadImages();
  LoadMeshes();
  InitScene();

  InitImGui();
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
  vkDeviceWaitIdle(g_PlatformState.Device);

  g_RenderState.MainDeletionQueue.flush();

  vkDeviceWaitIdle(g_PlatformState.Device);

  vkDestroySurfaceKHR(g_PlatformState.Instance, g_PlatformState.Surface, nullptr);

  vmaDestroyAllocator(g_Allocator);

  vkDestroyDevice(g_PlatformState.Device, nullptr);

  vkb::destroy_debug_utils_messenger(g_PlatformState.Instance, g_PlatformState.DebugMessenger);

  vkDestroyInstance(g_PlatformState.Instance, nullptr);
}

}