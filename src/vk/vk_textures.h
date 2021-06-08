
namespace vk::util
{

  struct mipmap_info
  {
    u64 DataSize;
    u64 DataOffset;
  };
  

  allocated_image
  upload_image(int TexWidth, int TexHeight, VkFormat ImageFormat, allocated_buffer_untyped &StagingBuffer)
  {
    VkExtent3D imageExtent;
    imageExtent.width = static_cast<u32>(TexWidth);
    imageExtent.height = static_cast<u32>(TexHeight);
    imageExtent.depth = 1;
  
    VkImageCreateInfo dimg_info = vk::init::image_create_info(ImageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

    allocated_image newImage;
  
    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(vk::renderer::g_Allocator, &dimg_info, &dimg_allocinfo, &newImage.Image, &newImage.Allocation, nullptr);

    vk::renderer::ImmediateSubmit([&](VkCommandBuffer cmd)
    {
      VkImageSubresourceRange range;
      range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      range.baseMipLevel = 0;
      range.levelCount = 1;
      range.baseArrayLayer = 0;
      range.layerCount = 1;

      VkImageMemoryBarrier imageBarrier_toTransfer = {};
      imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

      imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageBarrier_toTransfer.image = newImage.Image;
      imageBarrier_toTransfer.subresourceRange = range;

      imageBarrier_toTransfer.srcAccessMask = 0;
      imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

      VkBufferImageCopy copyRegion = {};
      copyRegion.bufferOffset = 0;
      copyRegion.bufferRowLength = 0;
      copyRegion.bufferImageHeight = 0;

      copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      copyRegion.imageSubresource.mipLevel = 0;
      copyRegion.imageSubresource.baseArrayLayer = 0;
      copyRegion.imageSubresource.layerCount = 1;
      copyRegion.imageExtent = imageExtent;

      vkCmdCopyBufferToImage(cmd, StagingBuffer.Buffer, newImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

      VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;

      imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toReadable);
    });

    VkImageViewCreateInfo view_info = vk::init::imageview_create_info(ImageFormat, newImage.Image, VK_IMAGE_ASPECT_COLOR_BIT);

    vkCreateImageView(vk::renderer::g_PlatformState.Device, &view_info, nullptr, &newImage.DefaultView);

    vk::renderer::g_RenderState.MainDeletionQueue.push_function([=]()
    {
      vmaDestroyImage(vk::renderer::g_Allocator, newImage.Image, newImage.Allocation);
    });

    newImage.MipLevels = 1;
    return newImage;
  }

  allocated_image
  upload_image_mipmapped(int TexWidth, int TexHeight, VkFormat ImageFormat, allocated_buffer_untyped &StagingBuffer, std::vector<mipmap_info> Mips)
  {
    VkExtent3D imageExtent;
    imageExtent.width = static_cast<uint32_t>(TexWidth);
    imageExtent.height = static_cast<uint32_t>(TexHeight);
    imageExtent.depth = 1;

    VkImageCreateInfo dimg_info = vk::init::image_create_info(ImageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);

    dimg_info.mipLevels = (uint32_t)Mips.size();

    allocated_image newImage;

    VmaAllocationCreateInfo dimg_allocinfo = {};
    dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(vk::renderer::g_Allocator, &dimg_info, &dimg_allocinfo, &newImage.Image, &newImage.Allocation, nullptr);

    vk::renderer::ImmediateSubmit([&](VkCommandBuffer cmd)
    {
      VkImageSubresourceRange range;
      range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      range.baseMipLevel = 0;
      range.levelCount = (uint32_t)Mips.size();
      range.baseArrayLayer = 0;
      range.layerCount = 1;

      VkImageMemoryBarrier imageBarrier_toTransfer = {};
      imageBarrier_toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

      imageBarrier_toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageBarrier_toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageBarrier_toTransfer.image = newImage.Image;
      imageBarrier_toTransfer.subresourceRange = range;

      imageBarrier_toTransfer.srcAccessMask = 0;
      imageBarrier_toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toTransfer);

      for (int i = 0; i < Mips.size(); i++)
      {

        VkBufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = Mips[i].DataOffset;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = i;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imageExtent;

        vkCmdCopyBufferToImage(cmd, StagingBuffer.Buffer, newImage.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        imageExtent.width /= 2;
        imageExtent.height /= 2;
      }
      VkImageMemoryBarrier imageBarrier_toReadable = imageBarrier_toTransfer;

      imageBarrier_toReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageBarrier_toReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      imageBarrier_toReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageBarrier_toReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier_toReadable);
    });

    newImage.MipLevels = (uint32_t)Mips.size();

    VkImageViewCreateInfo view_info = vk::init::imageview_create_info(ImageFormat, newImage.Image, VK_IMAGE_ASPECT_COLOR_BIT);
    view_info.subresourceRange.levelCount = newImage.MipLevels;
    vkCreateImageView(vk::renderer::g_PlatformState.Device, &view_info, nullptr, &newImage.DefaultView);

    vk::renderer::g_RenderState.MainDeletionQueue.push_function([=]()
    {
      vmaDestroyImage(vk::renderer::g_Allocator, newImage.Image, newImage.Allocation);
    });

    return newImage;
  }

  allocated_image
  load_image_from_file(const char* FilePath)
  {
    allocated_image outImage = {};

    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(FilePath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels)
    {
      Assert(false);
    }

    void* pixel_ptr = pixels;
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    VkFormat image_format = VK_FORMAT_R8G8B8A8_UNORM;

    allocated_buffer_untyped stagingBuffer = vk::renderer::CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data;
    vmaMapMemory(vk::renderer::g_Allocator, stagingBuffer.Allocation, &data);

    memcpy(data, pixel_ptr, static_cast<size_t>(imageSize));

    vmaUnmapMemory(vk::renderer::g_Allocator, stagingBuffer.Allocation);
    stbi_image_free(pixels);
  
    outImage = upload_image(texWidth, texHeight, image_format, stagingBuffer);

    vmaDestroyBuffer(vk::renderer::g_Allocator, stagingBuffer.Buffer, stagingBuffer.Allocation);

    return outImage;
  }

  allocated_image
  load_image_from_asset(const char *filename)
  {
    allocated_image outImage;

    assets::asset_file file;
    assets::LoadBinaryFile(filename, file);

    assets::texture_info textureInfo = assets::ReadTextureInfo(&file);

    VkDeviceSize imageSize = textureInfo.Size;
    VkFormat image_format = VK_FORMAT_UNDEFINED;
    switch (textureInfo.Format)
    {
    case assets::texture_format::RGBA8:
      image_format = VK_FORMAT_R8G8B8A8_UNORM;
      break;
    default:
      Assert(false);
    }

    allocated_buffer_untyped stagingBuffer = vk::renderer::CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_UNKNOWN, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    std::vector<mipmap_info> mips;

    void *data;
    vmaMapMemory(vk::renderer::g_Allocator, stagingBuffer.Allocation, &data);
    size_t offset = 0;
    {

      for (int i = 0; i < textureInfo.Pages.size(); i++)
      {
        mipmap_info mip;
        mip.DataOffset = offset;
        mip.DataSize = textureInfo.Pages[i].OriginalSize;
        mips.push_back(mip);
        assets::UnpackTexturePage(&textureInfo, i, (char*)file.BinaryBlob.data(), (char*)data + offset);

        offset += mip.DataSize;
      }
    }
    
    vmaUnmapMemory(vk::renderer::g_Allocator, stagingBuffer.Allocation);

    outImage = upload_image_mipmapped(textureInfo.Pages[0].Width, textureInfo.Pages[0].Height, image_format, stagingBuffer, mips);

    vmaDestroyBuffer(vk::renderer::g_Allocator, stagingBuffer.Buffer, stagingBuffer.Allocation);

    return outImage;
  }
}