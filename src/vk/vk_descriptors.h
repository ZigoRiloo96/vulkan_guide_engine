

#include <initializer_list>

namespace vk::util::descriptor
{
  struct
  descriptor_layout_info
  {
    std::vector<VkDescriptorSetLayoutBinding> Bindings;
    bool operator==(const descriptor_layout_info &Other) const
    {
      if (Other.Bindings.size() != Bindings.size())
      {
        return false;
      }
      else
      {
        for (int i = 0; i < Bindings.size(); i++)
        {
          if (Other.Bindings[i].binding != Bindings[i].binding)
          {
            return false;
          }
          if (Other.Bindings[i].descriptorType != Bindings[i].descriptorType)
          {
            return false;
          }
          if (Other.Bindings[i].descriptorCount != Bindings[i].descriptorCount)
          {
            return false;
          }
          if (Other.Bindings[i].stageFlags != Bindings[i].stageFlags)
          {
            return false;
          }
        }
        return true;
      }
    }

    std::size_t
    Hash() const
    {
      using std::hash;
      using std::size_t;

      size_t result = hash<size_t>()(Bindings.size());

      for (const VkDescriptorSetLayoutBinding &b : Bindings)
      {
        size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

        result ^= hash<size_t>()(binding_hash);
      }

      return result;
    }
  };

  struct
  descriptor_layout_hash
  {
    u64
    operator()(const descriptor_layout_info &k) const
    {
      return k.Hash();
    }
  };

  const f32
  g_PoolSizes[] =
  {
    /* VK_DESCRIPTOR_TYPE_SAMPLER */                0.5f,
    /* VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER */ 4.0f,
    /* VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE */          4.0f,
    /* VK_DESCRIPTOR_TYPE_STORAGE_IMAGE */          1.0f,
    /* VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER */   1.0f,
    /* VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER */   1.0f,
    /* VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER */         2.0f,
    /* VK_DESCRIPTOR_TYPE_STORAGE_BUFFER */         2.0f,
    /* VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC */ 1.0f,
    /* VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC */ 1.0f,
    /* VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT */       0.5f
  };

  VkDescriptorPool
  CreatePool(VkDevice Device, i32 Count, VkDescriptorPoolCreateFlags Flags)
  {
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(ArrayCount(g_PoolSizes));

    for (i32 i = 0; i < ArrayCount(g_PoolSizes); i++)
    {
      sizes.push_back( { (VkDescriptorType)i, uint32_t(g_PoolSizes[i] * Count) } );
    }

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = Flags;
    pool_info.maxSets = Count;
    pool_info.poolSizeCount = (uint32_t)sizes.size();
    pool_info.pPoolSizes = sizes.data();

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(Device, &pool_info, nullptr, &descriptorPool);

    return descriptorPool;
  }

  struct
  allocator
  {
    void
    ResetPools()
    {
      for (auto p : UsedPools)
      {
        vkResetDescriptorPool(Device, p, 0);
      }

      FreePools = UsedPools;
      UsedPools.clear();
      CurrentPool = VK_NULL_HANDLE;
    }

    void
    Cleanup()
    {
      for (auto p : FreePools)
      {
        vkDestroyDescriptorPool(Device, p, nullptr);
      }

      for (auto p : UsedPools)
      {
        vkDestroyDescriptorPool(Device, p, nullptr);
      }
    }

    void
    Allocate(VkDescriptorSet *Set, VkDescriptorSetLayout Layout)
    {
      if (CurrentPool == VK_NULL_HANDLE)
      {
        CurrentPool = GrabPool();
        UsedPools.push_back(CurrentPool);
      }

      VkDescriptorSetAllocateInfo allocInfo = {};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.pNext = nullptr;

      allocInfo.pSetLayouts = &Layout;
      allocInfo.descriptorPool = CurrentPool;
      allocInfo.descriptorSetCount = 1;

      VkResult allocResult = vkAllocateDescriptorSets(Device, &allocInfo, Set);
      bool needReallocate = false;

      switch (allocResult)
      {
      case VK_SUCCESS:
        return;

        break;
      case VK_ERROR_FRAGMENTED_POOL:
      case VK_ERROR_OUT_OF_POOL_MEMORY:
        needReallocate = true;
        break;
      default:
        Assert(false);
      }

      if (needReallocate)
      {
        CurrentPool = GrabPool();
        UsedPools.push_back(CurrentPool);

        allocResult = vkAllocateDescriptorSets(Device, &allocInfo, Set);

        if (allocResult == VK_SUCCESS)
        {
          return;
        }
      }

      Assert(false);
    }

    VkDescriptorPool
    GrabPool()
    {
      if (FreePools.size() > 0)
      {
        VkDescriptorPool pool = FreePools.back();
        FreePools.pop_back();
        return pool;
      }
      else
      {
        return CreatePool(Device, 1000, 0);
      }
    }

    VkDevice Device{VK_NULL_HANDLE};
    VkDescriptorPool CurrentPool{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> UsedPools;
    std::vector<VkDescriptorPool> FreePools;
  };

  struct
  cache
  {
    VkDescriptorSetLayout
    CreateDescriptorLayout(VkDescriptorSetLayoutCreateInfo *Info)
    {
      descriptor_layout_info layoutinfo;
      layoutinfo.Bindings.reserve(Info->bindingCount);
      bool isSorted = true;
      int32_t lastBinding = -1;
      for (uint32_t i = 0; i < Info->bindingCount; i++)
      {
        layoutinfo.Bindings.push_back(Info->pBindings[i]);

        if (static_cast<int32_t>(Info->pBindings[i].binding) > lastBinding)
        {
          lastBinding = Info->pBindings[i].binding;
        }
        else
        {
          isSorted = false;
        }
      }
      if (!isSorted)
      {
        std::sort(layoutinfo.Bindings.begin(), layoutinfo.Bindings.end(), [](VkDescriptorSetLayoutBinding &a, VkDescriptorSetLayoutBinding &b) {
          return a.binding < b.binding;
        });
      }

      auto it = LayoutCache.find(layoutinfo);
      if (it != LayoutCache.end())
      {
        return (*it).second;
      }
      else
      {
        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(Device, Info, nullptr, &layout);

        LayoutCache[layoutinfo] = layout;
        return layout;
      }
    }

    void
    cleanup()
    {
      for (auto pair : LayoutCache)
      {
        vkDestroyDescriptorSetLayout(Device, pair.second, nullptr);
      }
    }

    std::unordered_map<descriptor_layout_info, VkDescriptorSetLayout, descriptor_layout_hash> LayoutCache;
    VkDevice Device;
  };

  namespace builder
  {
    struct
    descriptor_bind
    {
      u32 Binding;
      VkDescriptorBufferInfo *BufferInfo;
      VkDescriptorImageInfo *ImageInfo;
      VkDescriptorType Type;
      VkShaderStageFlags StageFlags;
    };

    internal void
    BuildDescriptorSet(allocator& Alloc,
                        cache& Cache, 
                        VkDescriptorSet &DescriptorSet,
                        VkDescriptorSetLayout &Layout,
                        std::initializer_list<descriptor_bind> &Binds)
    {
      std::vector<VkDescriptorSetLayoutBinding> Bindings;
      std::vector<VkWriteDescriptorSet> Writes;

      for (auto &bind : Binds)
      {
        VkDescriptorSetLayoutBinding newBinding{};

        newBinding.descriptorCount = 1;
        newBinding.descriptorType = bind.Type;
        newBinding.pImmutableSamplers = nullptr;
        newBinding.stageFlags = bind.StageFlags;
        newBinding.binding = bind.Binding;

        Bindings.push_back(newBinding);

        VkWriteDescriptorSet newWrite{};
        newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        newWrite.pNext = nullptr;

        newWrite.descriptorCount = 1;
        newWrite.descriptorType = bind.Type;
        newWrite.pBufferInfo = bind.BufferInfo;
        newWrite.pImageInfo = bind.ImageInfo;
        newWrite.dstBinding = bind.Binding;

        Writes.push_back(newWrite);
      }

      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.pNext = nullptr;

      layoutInfo.pBindings = Bindings.data();
      layoutInfo.bindingCount = static_cast<uint32_t>(Bindings.size());

      Layout = Cache.CreateDescriptorLayout(&layoutInfo);

      Alloc.Allocate(&DescriptorSet, Layout);

      for (VkWriteDescriptorSet &w : Writes)
      {
        w.dstSet = DescriptorSet;
      }

      vkUpdateDescriptorSets(Alloc.Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0, nullptr);
    }

    internal void
    BuildDescriptorSet(allocator& Alloc,
                        cache& Cache,
                        VkDescriptorSet &DescriptorSet,
                        std::initializer_list<descriptor_bind> &Binds)
    {
      VkDescriptorSetLayout Layout;
      BuildDescriptorSet(Alloc, Cache, DescriptorSet, Layout, Binds);
    }

  }
}