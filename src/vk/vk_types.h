
#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

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

#include "vk_descriptors.h"

#include "../mesh.h"
