
#include <spirv_reflect.h>

struct shader_module
{
  std::vector<u32> Code;
  VkShaderModule Module;
};

namespace vk::util
{
  void LoadShaderModule(const char* filePath, shader_module* outShaderModule)
  {
    FILE* file = fopen(filePath, "rb");

    Assert(file)
  
    fseek(file, 0L, SEEK_END);
    u64 fileSize = ftell(file);
    fseek(file, 0L, SEEK_SET);
    std::vector<u32> buffer(fileSize / sizeof(u32));
    fread(buffer.data(), fileSize, 1, file);
    fclose(file);

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    createInfo.codeSize = buffer.size() * sizeof(u32); 
    createInfo.pCode = buffer.data();
  
    VkShaderModule shaderModule;
    Assert(vkCreateShaderModule(vk::renderer::g_PlatformState.Device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

    outShaderModule->Code = std::move(buffer);
    outShaderModule->Module = shaderModule;
  }

  constexpr u32
  fnv1a_32(const char* s, size_t count)
  {
    return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
  }

  u32 HashDescriptorLayoutInfo(VkDescriptorSetLayoutCreateInfo* info)
  {
    std::stringstream ss;

    ss << info->flags;
    ss << info->bindingCount;

    for (auto i = 0u; i < info->bindingCount; i++)
    {
      const VkDescriptorSetLayoutBinding& binding = info->pBindings[i];

      ss << binding.binding;
      ss << binding.descriptorCount;
      ss << binding.descriptorType;
      ss << binding.stageFlags;
    }

    auto str = ss.str();

    return fnv1a_32(str.c_str(), str.length());
  }
} // namespace vk::util

namespace vk::shader
{

struct
shader_effect
{

  struct reflection_overrides
  {
    const char* Name;
    VkDescriptorType OverridenType;
  };

  void
  AddStage(shader_module* shaderModule, VkShaderStageFlagBits stage)
  {
    shader_stage newStage = {shaderModule, stage};
    Stages.push_back(newStage);
  }
  
  struct descriptor_set_layout_data
  {
    u32 SetNumber;
    VkDescriptorSetLayoutCreateInfo CreateInfo;
    std::vector<VkDescriptorSetLayoutBinding> Bindings;
  };

  void
  ReflectLayout(reflection_overrides* overrides, i32 overrideCount)
  {
    std::vector<descriptor_set_layout_data> set_layouts;
    std::vector<VkPushConstantRange> constant_ranges;

    for (auto &s : Stages)
    {
      SpvReflectShaderModule spvmodule;
      SpvReflectResult result = spvReflectCreateShaderModule(s.Module->Code.size() * sizeof(uint32_t), s.Module->Code.data(), &spvmodule);

      uint32_t count = 0;
      result = spvReflectEnumerateDescriptorSets(&spvmodule, &count, NULL);
      Assert(result == SPV_REFLECT_RESULT_SUCCESS);

      std::vector<SpvReflectDescriptorSet *> sets(count);
      result = spvReflectEnumerateDescriptorSets(&spvmodule, &count, sets.data());
      Assert(result == SPV_REFLECT_RESULT_SUCCESS);

      for (size_t i_set = 0; i_set < sets.size(); ++i_set)
      {
        const SpvReflectDescriptorSet &refl_set = *(sets[i_set]);

        descriptor_set_layout_data layout = {};

        layout.Bindings.resize(refl_set.binding_count);
        for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding)
        {
          const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);
          VkDescriptorSetLayoutBinding &layout_binding = layout.Bindings[i_binding];
          layout_binding.binding = refl_binding.binding;
          layout_binding.descriptorType = static_cast<VkDescriptorType>(refl_binding.descriptor_type);

          for (int ov = 0; ov < overrideCount; ov++)
          {
            if (strcmp(refl_binding.name, overrides[ov].Name) == 0)
            {
              layout_binding.descriptorType = overrides[ov].OverridenType;
            }
          }

          layout_binding.descriptorCount = 1;
          for (uint32_t i_dim = 0; i_dim < refl_binding.array.dims_count; ++i_dim)
          {
            layout_binding.descriptorCount *= refl_binding.array.dims[i_dim];
          }
          layout_binding.stageFlags = static_cast<VkShaderStageFlagBits>(spvmodule.shader_stage);

          reflected_binding reflected;
          reflected.Binding = layout_binding.binding;
          reflected.Set = refl_set.set;
          reflected.Type = layout_binding.descriptorType;

          Bindings[refl_binding.name] = reflected;
        }
        layout.SetNumber = refl_set.set;
        layout.CreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.CreateInfo.bindingCount = refl_set.binding_count;
        layout.CreateInfo.pBindings = layout.Bindings.data();

        set_layouts.push_back(layout);
      }

      //pushconstants

      result = spvReflectEnumeratePushConstantBlocks(&spvmodule, &count, NULL);
      Assert(result == SPV_REFLECT_RESULT_SUCCESS);

      std::vector<SpvReflectBlockVariable *> pconstants(count);
      result = spvReflectEnumeratePushConstantBlocks(&spvmodule, &count, pconstants.data());
      Assert(result == SPV_REFLECT_RESULT_SUCCESS);

      if (count > 0)
      {
        VkPushConstantRange pcs{};
        pcs.offset = pconstants[0]->offset;
        pcs.size = pconstants[0]->size;
        pcs.stageFlags = s.Stage;

        constant_ranges.push_back(pcs);
      }
    }
  }

  void
  FillStages(std::vector<VkPipelineShaderStageCreateInfo>& pipelineStages)
  {
    for (auto& s : Stages)
	  {
		  pipelineStages.push_back(vk::init::pipeline_shader_stage_create_info(s.Stage, s.Module->Module));
	  }
  }

  VkPipelineLayout BuiltLayout;

  struct reflected_binding
  {
    u32 Set;
    u32 Binding;
    VkDescriptorType Type;
  };

  std::unordered_map<std::string, reflected_binding> Bindings;
  std::array<VkDescriptorSetLayout, 4> SetLayouts;
  std::array<u32, 4> SetHashes;

  struct shader_stage
  {
    shader_module* Module;
    VkShaderStageFlagBits Stage;
  };

  std::vector<shader_stage> Stages;
  
  // other
};

struct
shader_descriptor_binder
{
  struct buffer_write_desriptor
  {
    u32 DstSet;
    u32 DstBinding;
    VkDescriptorType DescriptorType;
    VkDescriptorBufferInfo BufferInfo;

    u32 DynamicOffset;
  };

  void
  BindBuffer(const char* name, const VkDescriptorBufferInfo& bufferInfo)
  {
    BindDynamicBuffer(name, (u32)-1, bufferInfo);
  }

  void
  BindDynamicBuffer(const char* name, u32 offset, const VkDescriptorBufferInfo& bufferInfo)
  {
    auto found = Shaders->Bindings.find(name);
	  if (found != Shaders->Bindings.end())
    {
		  const shader_effect::reflected_binding& bind = (*found).second;

		  for (auto& write : BufferWrites)
      {
			  if (write.DstBinding == bind.Binding
				    && write.DstSet == bind.Set)
			  {
				  if (write.BufferInfo.buffer != bufferInfo.buffer ||
					    write.BufferInfo.range != bufferInfo.range ||
					    write.BufferInfo.offset != bufferInfo.offset) 
				  {
					  write.BufferInfo = bufferInfo;
					  write.DynamicOffset = offset;

					  CachedDescriptorSets[write.DstSet] = VK_NULL_HANDLE;
				  }
				  else
          {
					  //already in the write list, but matches buffer
  					write.DynamicOffset = offset;
				  }

  				return;
			  }
		  }

      buffer_write_desriptor newWrite;
		  newWrite.DstSet = bind.Set;
		  newWrite.DstBinding = bind.Binding;
		  newWrite.DescriptorType = bind.Type;
		  newWrite.BufferInfo = bufferInfo;
		  newWrite.DynamicOffset = offset;

		  CachedDescriptorSets[bind.Set] = VK_NULL_HANDLE;

		  BufferWrites.push_back(newWrite);
	  }
  }

  void
  ApplyBinds(VkCommandBuffer cmd)
  {
    for (int i = 0; i < 2; i++)
    {
		  //there are writes for this set
		  if (CachedDescriptorSets[i] != VK_NULL_HANDLE)
      {
			  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Shaders->BuiltLayout, i, 1, &CachedDescriptorSets[i], SetOffsets[i].Count, SetOffsets[i].Offsets.data());
		  }
	  }
  }

  void
  BuiltSets(vk::descriptor::allocator& allocator)
  {
    std::array<std::vector<VkWriteDescriptorSet>, 4> writes{};

    std::sort(BufferWrites.begin(), BufferWrites.end(), [](buffer_write_desriptor &a, buffer_write_desriptor &b)
    {
                if (b.DstSet == a.DstSet)
                {
                  return a.DstSet < b.DstSet;
                }
                else
                {
                  return a.DstBinding < b.DstBinding;
                }
    });

    //reset the dynamic offsets
    for (auto &s : SetOffsets)
    {
      s.Count = 0;
    }

    for (buffer_write_desriptor &w : BufferWrites)
    {
      u32 set = w.DstSet;
      VkWriteDescriptorSet write = vk::init::write_descriptor_buffer(w.DescriptorType, VK_NULL_HANDLE, &w.BufferInfo, w.DstBinding);

      writes[set].push_back(write);

      //dynamic offsets
      if (w.DescriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || w.DescriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      {
        dyn_offsets &offsetSet = SetOffsets[set];
        offsetSet.Offsets[offsetSet.Count] = w.DynamicOffset;
        offsetSet.Count++;
      }
    }

    for (int i = 0; i < 4; i++)
    {
      //there are writes for this set
      if (writes[i].size() > 0)
      {

        if (CachedDescriptorSets[i] == VK_NULL_HANDLE)
        {
          //alloc
          auto layout = Shaders->SetLayouts[i];

          VkDescriptorSet newDescriptor;

          allocator.Allocate(&newDescriptor, layout);

          for (auto &w : writes[i])
          {
            w.dstSet = newDescriptor;
          }

          vkUpdateDescriptorSets(vk::renderer::g_PlatformState.Device, (u32)writes[i].size(), writes[i].data(), 0, nullptr);

          CachedDescriptorSets[i] = newDescriptor;
        }
      }
    }
  }

  void
  SetShader(shader_effect* newShader)
  {
    if (Shaders && Shaders != newShader)
    {
      for (int i = 0; i < 4; i++)
      {
        if (newShader->SetHashes[i] != Shaders->SetHashes[i])
        {
          CachedDescriptorSets[i] = VK_NULL_HANDLE;
        }
        else if (newShader->SetHashes[i] == 0)
        {
          CachedDescriptorSets[i] = VK_NULL_HANDLE;
        }
      }
    }
    else
    {
      for (int i = 0; i < 4; i++)
      {
        CachedDescriptorSets[i] = VK_NULL_HANDLE;
      }
    }

    Shaders = newShader;
  }

  std::array<VkDescriptorSet, 4> CachedDescriptorSets;

  struct dyn_offsets
  {
    std::array<u32, 16> Offsets;
    u32 Count;
  };

  std::array<dyn_offsets, 4> SetOffsets;

  shader_effect* Shaders {nullptr};

  std::vector<buffer_write_desriptor> BufferWrites;  
};

struct shader_cache
{
  shader_module*
  GetShader(const std::string& path)
  {
    auto it = ModuleCache.find(path);
    if (it == ModuleCache.end())
    {
      shader_module newShader;

      vk::util::LoadShaderModule(path.c_str(), &newShader);

      ModuleCache[path] = newShader;
    }
    return &ModuleCache[path];
  }

  std::unordered_map<std::string, shader_module> ModuleCache;
};

} // namespace vk::shader