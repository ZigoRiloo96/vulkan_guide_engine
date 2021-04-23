
#include "vk_initializers.h"

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
RenderObject
{
  Mesh* Mesh;
  Material* Material;
  glm::mat4 TransformMatrix;
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
	VkCommandPool CommandPool;
	VkCommandBuffer MainCommandBuffer;

  allocated_buffer CameraBuffer;
  allocated_buffer ObjectBuffer;

  VkDescriptorSet ObjectDescriptor;
  VkDescriptorSet GlobalDescriptor;
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

  // Depth
  VkImageView DepthImageView;
  allocated_image DepthImage;
  VkFormat DepthFormat;

  // Renderables
  std::vector<RenderObject> Renderables;
  std::unordered_map<std::string, Material> Materials;
  std::unordered_map<std::string, Mesh> Meshes;

  // Scene
  GPUSceneData SceneParameters;
	allocated_buffer SceneParameterBuffer;

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