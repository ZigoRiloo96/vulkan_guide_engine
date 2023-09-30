
namespace vk::util
{
  struct push_buffer
  {
		template<typename T>
		u32 push(T& data)
    {
      return push(&data, sizeof(T));
    }

		u32 push(void* data, size_t size)
    {
      u32 offset = currentOffset;
	    char* target = (char*)mapped;
	    target += currentOffset;
	    memcpy(target, data, size);
	    currentOffset += static_cast<u32>(size);
	    currentOffset = pad_uniform_buffer_size(currentOffset);

	    return offset;
    }

		void init(VmaAllocator& allocator, allocated_buffer_untyped sourceBuffer, u32 alignement)
    {
      align = alignement;
	    source = sourceBuffer;
	    currentOffset = 0;
	    vmaMapMemory(allocator, sourceBuffer.Allocation, &mapped);
    }

		void reset()
    {
      currentOffset = 0;
    }

		u32 pad_uniform_buffer_size(u32 originalSize)
    {
      size_t minUboAlignment = align;
	    size_t alignedSize = originalSize;
	    if (minUboAlignment > 0)
      {
		    alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	    }
	    return static_cast<u32>(alignedSize);
    }
    
		allocated_buffer_untyped source;
		u32 align;
		u32 currentOffset;
		void* mapped;
	};
}