
#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

// struct allocated_buffer
// {
//   VkBuffer Buffer;
//   VmaAllocation Allocation;
//   VkDeviceSize Size{0};
// };

struct allocated_image
{
  VkImage Image;
  VmaAllocation Allocation;

  VkImageView DefaultView;
  int MipLevels;
};

struct
allocated_buffer_untyped
{
	VkBuffer Buffer{};
	VmaAllocation Allocation{};
	VkDeviceSize Size{0};
	VkDescriptorBufferInfo
  GetInfo(VkDeviceSize offset = 0);
};

template<typename T>
struct
allocated_buffer : public allocated_buffer_untyped
{
	void
  operator=(const allocated_buffer_untyped& other)
  {
		Buffer = other.Buffer;
		Allocation = other.Allocation;
		Size = other.Size;
	}

	allocated_buffer(allocated_buffer_untyped& other)
  {
		Buffer = other.Buffer;
		Allocation = other.Allocation;
		Size = other.Size;
	}
  
	allocated_buffer() = default;
};

struct
VertexInputDescription
{
  std::vector<VkVertexInputBindingDescription> Bindings;
  std::vector<VkVertexInputAttributeDescription> Attributes;

  VkPipelineVertexInputStateCreateFlags Flags = 0;
};

enum class MeshpassType : u8
{
	None = 0,
	Forward = 1,
	Transparency = 2,
	DirectionalShadow = 3
};


#include "vk_descriptors.h"

#include "../mesh.h"
