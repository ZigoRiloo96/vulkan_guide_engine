
#include "vk_initializers.h"

#include "vk_pushbuffer.h"
#include "vk_descriptors.h"
#include "vk_shaders.h"

#include "../mesh.h"

#define VK_CHECK(x)                                               \
  do                                                              \
  {                                                               \
    VkResult err = x;                                             \
    if (err)                                                      \
    {                                                             \
      logger::err("[VK] %d", err);                                \
      Assert(false);                                              \
    }                                                             \
  } while (0)

namespace vk::renderer
{

constexpr unsigned int FRAME_OVERLAP = 3;

struct DeletionQueue
{
  std::deque<std::function<void()>> deletors;

  void push_function(std::function<void()>&& function)
  {
    deletors.push_back(function);
  }

  void flush()
  {
    for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
    {
      (*it)();
    }

    deletors.clear();
  }
};

struct
Material
{
  VkDescriptorSet TextureSet {VK_NULL_HANDLE};
  VkPipeline Pipeline;
  VkPipelineLayout PipelineLayout;
};

struct
Texture
{
  allocated_image Image;
  VkImageView ImageView;
};

struct
upload_context
{
  VkFence UploadFence;
  VkCommandPool CommandPool;
};

struct
render_object
{
  Mesh* Mesh;
  Material* Material;
  glm::mat4 TransformMatrix;
};

struct indirect_batch
{
  Mesh* Mesh;
  Material* Material;
  u32 First;
  u32 Count;
};

struct
GPUCameraData
{
  glm::mat4 View;
  glm::mat4 Projection;
  glm::mat4 Viewproj;
};

struct
GPUObjectData
{
  glm::mat4 ModelMatrix;
};

struct
GPUSceneData
{
  glm::vec4 FogColor;
  glm::vec4 FogDistances;
  glm::vec4 AmbientColor;
  glm::vec4 SunlightDirection;
  glm::vec4 SunlightColor;
};

struct
FrameData
{
  VkSemaphore PresentSemaphore, RenderSemaphore;
  VkFence RenderFence;

  // DeletionQueue FrameDeletionQueue;

  VkCommandPool CommandPool;
  VkCommandBuffer MainCommandBuffer;

  vk::util::push_buffer DynamicData;

  allocated_buffer_untyped DebugOutputBuffer;

  vk::util::descriptor::allocator* DynamicDescriptorAllocator;

  allocated_buffer_untyped CameraBuffer;
  allocated_buffer_untyped ObjectBuffer;

  VkDescriptorSet ObjectDescriptor;
  VkDescriptorSet GlobalDescriptor;

  std::vector<u32> DebugDataOffsets;
	std::vector<std::string> DebugDataNames;
};

struct vulkan_platform_state
{
  HWND Win32NativeWindow;
  VkInstance Instance;
  VkSurfaceKHR Surface;
  VkDebugUtilsMessengerEXT DebugMessenger;

  VkPhysicalDevice ChosenGPU;
  VkDevice Device;

  VkPhysicalDeviceProperties GpuProperties;

  VkExtent2D WindowExtent;
};

struct
vulkan_render_state
{
  VkSwapchainKHR Swapchain;
  VkFormat SwapchainImageFormat;

  std::vector<VkImage> SwapchainImages;
  std::vector<VkImageView> SwapchainImageViews;

  VkQueue GraphicsQueue;
  u32 GraphicsQueueFamily;

  VkRenderPass RenderPass;
  VkRenderPass CopyPass;
  VkRenderPass ShadowPass;
  std::vector<VkFramebuffer> Framebuffers;

  i32 FrameNumber = 0;
  FrameData Frames[FRAME_OVERLAP];

  FrameData&
  GetCurrentFrame()
  {
    return Frames[FrameNumber % FRAME_OVERLAP];
  }

  // Pipeline
  VkPipelineLayout TrianglePipelineLayout;
  VkPipelineLayout MeshPipelineLayout;

  VkPipeline TrianglePipeline;
  VkPipeline RedTrianglePipeline;
  VkPipeline MeshPipeline;

  Mesh TriangleMesh;
  Mesh ObjMesh;

  // Descriptors
  VkDescriptorSetLayout GlobalSetLayout;
  VkDescriptorSetLayout ObjectSetLayout;
  VkDescriptorSetLayout SingleTextureSetLayout;
  VkDescriptorPool DescriptorPool;

  // render
  allocated_image RawRenderImage;
  VkSampler SmoothSampler;
  VkFramebuffer ForwardFramebuffer;
	VkFramebuffer ShadowFramebuffer;

  // Depth
  VkImageView DepthImageView;
  allocated_image DepthImage;
  allocated_image DepthPyramid;
  VkFormat DepthFormat;

  vk::util::descriptor::allocator* DescriptorAllocator;
	vk::util::descriptor::cache* DescriptorLayoutCache;
	// vkutil::VulkanProfiler* _profiler;
	// vkutil::MaterialSystem* _materialSystem;

  allocated_image ShadowImage;
  VkSampler ShadowSampler;
  VkExtent2D ShadowExtent{ 1024*4,1024*4 };
	int DepthPyramidWidth ;
	int DepthPyramidHeight;
	int DepthPyramidLevels;

  // Renderables
  std::vector<render_object> Renderables;
  std::unordered_map<std::string, Material> Materials;
  std::unordered_map<std::string, Mesh> Meshes;

  // samplers
  VkSampler DepthSampler;
	VkImageView DepthPyramidMips[16] = {};

  // Scene
  GPUSceneData SceneParameters;
  allocated_buffer_untyped SceneParameterBuffer;

  // Context
  upload_context UploadContext;

  // Textures
  std::unordered_map<std::string, Texture> LoadedTextures;

  // Deletion queue
  DeletionQueue MainDeletionQueue;
};

struct
pipeline_builder
{
  std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
  VkPipelineVertexInputStateCreateInfo VertexInputInfo;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly;
  VkViewport Viewport;
  VkRect2D Scissor;
  VkPipelineRasterizationStateCreateInfo Rasterizer;
  VkPipelineColorBlendAttachmentState ColorBlendAttachment;
  VkPipelineMultisampleStateCreateInfo Multisampling;
  VkPipelineLayout PipelineLayout;
  VkPipelineDepthStencilStateCreateInfo DepthStencil;
};

}