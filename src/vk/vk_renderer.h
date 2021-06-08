
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

constexpr unsigned int FRAME_OVERLAP = 1;
// const int MAX_OBJECTS = 150000;

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

struct GPUObjectData
{
	glm::mat4 ModelMatrix;
	glm::vec4 OriginRad; // bounds
	glm::vec4 Extents;  // bounds
};

struct
GPUSceneData
{
  glm::vec4 FogColor; // w is for exponent
	glm::vec4 FogDistances; //x for min, y for max, zw unused.
	glm::vec4 AmbientColor;
	glm::vec4 SunlightDirection; //w for sun power
	glm::vec4 SunlightColor;
	glm::mat4 SunlightShadowMatrix;
};

struct
FrameData
{
  VkSemaphore PresentSemaphore, RenderSemaphore;
  VkFence RenderFence;  
  VkCommandPool CommandPool;
  VkCommandBuffer MainCommandBuffer;

  allocated_buffer<GPUCameraData> CameraBuffer;
  allocated_buffer<GPUObjectData> ObjectBuffer;

  VkDescriptorSet ObjectDescriptor;
  VkDescriptorSet GlobalDescriptor;
};

struct /*alignas(16)*/DrawCullData
{
	glm::mat4 viewMat;
	float P00, P11, znear, zfar; // symmetric projection parameters
	float frustum[4]; // data for left/right/top/bottom frustum planes
	float lodBase, lodStep; // lod distance i = base * pow(step, i)
	float pyramidWidth, pyramidHeight; // depth pyramid size in texels

	uint32_t drawCount;

	int cullingEnabled;
	int lodEnabled;
	int occlusionEnabled;
	int distanceCheck;
	int AABBcheck;
	float aabbmin_x;
	float aabbmin_y;
	float aabbmin_z;
	float aabbmax_x;
	float aabbmax_y;
	float aabbmax_z;	
};

//struct EngineConfig {
//	//float drawDistance{5000};
//	//float shadowBias{ 5.25f };
//	//float shadowBiasslope{4.75f };
//	//bool occlusionCullGPU{ true };
//	//bool frustrumCullCPU{ true };
//	//bool outputIndirectBufferToFile{false};
//	//bool freezeCulling{ false };
//	//bool mouseLook{ true };
//};

struct DirectionalLight
{
	glm::vec3 lightPosition;
	glm::vec3 lightDirection;
	glm::vec3 shadowExtent;
	glm::mat4 get_projection();

	glm::mat4 get_view();
};
struct CullParams
{
	glm::mat4 viewmat;
	glm::mat4 projmat;
	bool occlusionCull;
	bool frustrumCull;
	float drawDist;
	bool aabb;
	glm::vec3 aabbmin;
	glm::vec3 aabbmax;
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
  allocated_buffer<vk::renderer::GPUSceneData> SceneParameterBuffer;

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