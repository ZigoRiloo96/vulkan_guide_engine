
namespace vk::util
{

struct
push_buffer
{
  template<typename T>
	u32 Push(T& data)
  {
    return Push(&data, sizeof(T));
  }

	u32
  Push(void* data, u64 size)
  {
    u32 offset = CurrentOffset;
    char *target = (char *)Mapped;
    target += CurrentOffset;
    memcpy(target, data, size);
    CurrentOffset += static_cast<u32>(size);
    CurrentOffset = PadUniformBufferSize(CurrentOffset);

    return offset;
  }

	void
  Init(VmaAllocator& allocator,
       allocated_buffer_untyped sourceBuffer,
       u32 alignement)
  {
    Align = alignement;
	  Source = sourceBuffer;
	  CurrentOffset = 0;
	  vmaMapMemory(allocator, sourceBuffer.Allocation, &Mapped);
  }
            
	void
  Reset()
  {
    CurrentOffset = 0;
  }

	u32
  PadUniformBufferSize(u32 originalSize)
  {
    // Calculate required alignment based on minimum device offset alignment
    size_t minUboAlignment = Align;
    size_t alignedSize = originalSize;
    if (minUboAlignment > 0)
    {
      alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
    }
    return static_cast<u32>(alignedSize);
  }

	allocated_buffer_untyped Source;
	u32 Align;
	u32 CurrentOffset;
	void* Mapped;
};

}