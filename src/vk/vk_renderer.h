
#include "vk_initializers.h"

#include "vk_pushbuffer.h"

#include "../player_camera.h"

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

struct render_scene;

namespace vk::shader
{
struct shader_cache;
}

namespace vk::util
{
struct material_system;
struct material;
}

namespace vk::renderer
{

constexpr unsigned int FRAME_OVERLAP = 2;
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

// struct
// Material
// {
//   VkDescriptorSet TextureSet {VK_NULL_HANDLE};
//   VkPipeline Pipeline;
//   VkPipelineLayout PipelineLayout;
// };

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
  vk::util::material* Material;
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

struct EngineStats
{
	float frametime;
	int objects;
	int drawcalls;
	int draws;
	int triangles;
};

struct
FrameData
{
  VkSemaphore PresentSemaphore, RenderSemaphore;
  VkFence RenderFence;

  DeletionQueue FrameDeletionQueue;

  VkCommandPool CommandPool;
  VkCommandBuffer MainCommandBuffer;

  allocated_buffer<GPUCameraData> CameraBuffer;
  allocated_buffer<GPUObjectData> ObjectBuffer;

  vk::util::push_buffer DynamicData;

  allocated_buffer_untyped DebugOutputBuffer;

  VkDescriptorSet ObjectDescriptor;
  VkDescriptorSet GlobalDescriptor;

  vk::descriptor::allocator* DynamicDescriptorAllocator;
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
	glm::mat4 get_projection()
  {
    glm::mat4 projection = glm::orthoLH_ZO(-shadowExtent.x, shadowExtent.x, -shadowExtent.y, shadowExtent.y, -shadowExtent.z, shadowExtent.z);
	  return projection;
  }
	glm::mat4 get_view()
  {
    glm::vec3 camPos = lightPosition;

	  glm::vec3 camFwd = lightDirection;

	  glm::mat4 view = glm::lookAt(camPos, camPos + camFwd, glm::vec3(1, 0, 0));
	  return view;
  }
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
  VkRenderPass ShadowPass;
	VkRenderPass CopyPass;

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
  allocated_image DepthPyramid;
  VkFormat DepthFormat;

  // Renderables
  std::vector<RenderObject> Renderables;
  std::unordered_map<std::string, vk::util::material> Materials;
  std::unordered_map<std::string, Mesh> Meshes;
  std::unordered_map<std::string, Texture> LoadedTextures;
  // std::unordered_map<std::string, assets::PrefabInfo*> _prefabCache;

  vk::shader::shader_cache* ShaderCache;

  // Scene
  GPUSceneData SceneParameters;
  allocated_buffer<vk::renderer::GPUSceneData> SceneParameterBuffer;

  // Context
  upload_context UploadContext;

  // Deletion queue
  DeletionQueue MainDeletionQueue;

  render_scene* RenderScene;
  VkSampler DepthSampler;
  VkImageView DepthPyramidMips[16] = {};

  vk::descriptor::allocator* DescriptorAllocator;
	vk::descriptor::cache* DescriptorLayoutCache;

  VkPipeline CullPipeline;
	VkPipelineLayout CullLayout;

  std::vector<VkBufferMemoryBarrier> UploadBarriers;
	std::vector<VkBufferMemoryBarrier> CullReadyBarriers;
	std::vector<VkBufferMemoryBarrier> PostCullBarriers;

  i32 DepthPyramidWidth;
	i32 DepthPyramidHeight;
	i32 DepthPyramidLevels;

  VkPipeline DepthReducePipeline;
	VkPipelineLayout DepthReduceLayout;

  VkPipeline SparseUploadPipeline;
	VkPipelineLayout SparseUploadLayout;

  VkPipeline BlitPipeline;
	VkPipelineLayout BlitLayout;

  player_camera Camera;
  DirectionalLight MainLight;

  allocated_image ShadowImage;
  VkExtent2D ShadowExtent{ 1024,1024 };

  VkSampler ShadowSampler;

  EngineStats stats;

  VkFormat RenderFormat;
	allocated_image RawRenderImage;
  VkSampler SmoothSampler;
  VkFramebuffer ForwardFramebuffer;
	VkFramebuffer ShadowFramebuffer;

  vk::util::material_system* MaterialSystem;

  bool IsInitialized = false;
};

// struct
// pipeline_builder
// {
//   std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
//   VkPipelineVertexInputStateCreateInfo VertexInputInfo;
//   VkPipelineInputAssemblyStateCreateInfo InputAssembly;
//   VkViewport Viewport;
//   VkRect2D Scissor;
//   VkPipelineRasterizationStateCreateInfo Rasterizer;
//   VkPipelineColorBlendAttachmentState ColorBlendAttachment;
//   VkPipelineMultisampleStateCreateInfo Multisampling;
//   VkPipelineLayout PipelineLayout;
//   VkPipelineDepthStencilStateCreateInfo DepthStencil;
// };

}