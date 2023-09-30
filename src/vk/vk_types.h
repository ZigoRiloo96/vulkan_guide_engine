
#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

struct allocated_buffer_untyped
{
	VkBuffer Buffer{};
	VmaAllocation Allocation{};
	VkDeviceSize Size{0};
	VkDescriptorBufferInfo get_info(VkDeviceSize offset = 0)
  {
    VkDescriptorBufferInfo info;
	  info.buffer = Buffer;
	  info.offset = offset;
	  info.range = Size;
	  return info;
  }
};

struct allocated_buffer
{
  VkBuffer Buffer;
  VmaAllocation Allocation;
  VkDeviceSize Size{0};
};

struct allocated_image
{
  VkImage Image;
  VmaAllocation Allocation;

  VkImageView DefaultView;
  int MipLevels;
};

struct
VertexInputDescription
{
  std::vector<VkVertexInputBindingDescription> Bindings;
  std::vector<VkVertexInputAttributeDescription> Attributes;

  VkPipelineVertexInputStateCreateFlags Flags = 0;
};

// material
