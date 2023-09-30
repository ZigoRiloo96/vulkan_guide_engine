
#include <spirv_reflect.h>

namespace vk::util
{
  internal VkShaderModule
  LoadShaderModule(VkDevice Device, const char* filePath)
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
    Assert(vkCreateShaderModule(Device, &createInfo, nullptr, &shaderModule) == VK_SUCCESS);

    return shaderModule;
  }

  constexpr u32 fnv1a_32(char const* s, std::size_t count)
  {
  	return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
  }
  
  u32 HashDescriptorLayoutInfo(VkDescriptorSetLayoutCreateInfo* info)
  {
	  //we are going to put all the data into a string and then hash the string
	  std::stringstream ss;

	  ss << info->flags;
	  ss << info->bindingCount;

	  for (auto i = 0u; i < info->bindingCount; i++)
    {
		  const VkDescriptorSetLayoutBinding &binding = info->pBindings[i];

		  ss << binding.binding;
		  ss << binding.descriptorCount;
		  ss << binding.descriptorType;
		  ss << binding.stageFlags;
	  }

	  auto str = ss.str();

	  return fnv1a_32(str.c_str(),str.length());
  }

} // namespace vk::util


namespace vk::shaders
{

  struct
  shader_module
  {
    std::vector<u32> Code;
    VkShaderModule Module;
  };

  struct
  shader_effect
  {
    struct reflection_overriders
    {
      const char* Name;
      VkDescriptorType OverridenType;
    };

    void AddStage(shader_module* ShaderModule, VkShaderStageFlagBits Stage)
    {
      shader_stage newStage = { ShaderModule, Stage };
      Stages.push_back(newStage);
    }

    struct DescriptorSetLayoutData
    {
	    u32 set_number;
	    VkDescriptorSetLayoutCreateInfo create_info;
	    std::vector<VkDescriptorSetLayoutBinding> bindings;
    };

    void ReflectLayout(VkDevice Device, reflection_overriders* Overrides, i32 OverrideCount)
    {
      std::vector<DescriptorSetLayoutData> set_layouts;

      std::vector<VkPushConstantRange> constant_ranges;

      for (auto &s : Stages)
      {
        SpvReflectShaderModule spvmodule;
        SpvReflectResult result = spvReflectCreateShaderModule(s.ShaderModule->Code.size() * sizeof(u32), s.ShaderModule->Code.data(), &spvmodule);

        u32 count = 0;
        result = spvReflectEnumerateDescriptorSets(&spvmodule, &count, NULL);
        Assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectDescriptorSet*> sets(count);
        result = spvReflectEnumerateDescriptorSets(&spvmodule, &count, sets.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        for (size_t i_set = 0; i_set < sets.size(); ++i_set)
        {
          const SpvReflectDescriptorSet &refl_set = *(sets[i_set]);

          DescriptorSetLayoutData layout = {};

          layout.bindings.resize(refl_set.binding_count);
          for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding)
          {
            const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);
            VkDescriptorSetLayoutBinding &layout_binding = layout.bindings[i_binding];
            layout_binding.binding = refl_binding.binding;
            layout_binding.descriptorType = static_cast<VkDescriptorType>(refl_binding.descriptor_type);

            for (int ov = 0; ov < OverrideCount; ov++)
            {
              if (strcmp(refl_binding.name, Overrides[ov].Name) == 0)
              {
                layout_binding.descriptorType = Overrides[ov].OverridenType;
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
          layout.set_number = refl_set.set;
          layout.create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
          layout.create_info.bindingCount = refl_set.binding_count;
          layout.create_info.pBindings = layout.bindings.data();

          set_layouts.push_back(layout);
        }

        //pushconstants

        result = spvReflectEnumeratePushConstantBlocks(&spvmodule, &count, NULL);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectBlockVariable *> pconstants(count);
        result = spvReflectEnumeratePushConstantBlocks(&spvmodule, &count, pconstants.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        if (count > 0)
        {
          VkPushConstantRange pcs{};
          pcs.offset = pconstants[0]->offset;
          pcs.size = pconstants[0]->size;
          pcs.stageFlags = s.Stage;

          constant_ranges.push_back(pcs);
        }
      }

      std::array<DescriptorSetLayoutData, 4> merged_layouts;

      for (u32 i = 0; i < 4; i++)
      {

        DescriptorSetLayoutData &ly = merged_layouts[i];

        ly.set_number = i;

        ly.create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

        std::unordered_map<int, VkDescriptorSetLayoutBinding> binds;
        for (auto &s : set_layouts)
        {
          if (s.set_number == i)
          {
            for (auto &b : s.bindings)
            {
              auto it = binds.find(b.binding);
              if (it == binds.end())
              {
                binds[b.binding] = b;
                //ly.bindings.push_back(b);
              }
              else
              {
                //merge flags
                binds[b.binding].stageFlags |= b.stageFlags;
              }
            }
          }
        }
        for (auto [k, v] : binds)
        {
          ly.bindings.push_back(v);
        }
        //sort the bindings, for hash purposes
        std::sort(ly.bindings.begin(), ly.bindings.end(), [](VkDescriptorSetLayoutBinding &a, VkDescriptorSetLayoutBinding &b) {
          return a.binding < b.binding;
        });

        ly.create_info.bindingCount = (uint32_t)ly.bindings.size();
        ly.create_info.pBindings = ly.bindings.data();
        ly.create_info.flags = 0;
        ly.create_info.pNext = 0;

        if (ly.create_info.bindingCount > 0)
        {
          SetHashes[i] = vk::util::HashDescriptorLayoutInfo(&ly.create_info);
          vkCreateDescriptorSetLayout(Device, &ly.create_info, nullptr, &SetLayouts[i]);
        }
        else
        {
          SetHashes[i] = 0;
          SetLayouts[i] = VK_NULL_HANDLE;
        }
      }

      //we start from just the default empty pipeline layout info
      VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vk::init::pipeline_layout_create_info();

      mesh_pipeline_layout_info.pPushConstantRanges = constant_ranges.data();
      mesh_pipeline_layout_info.pushConstantRangeCount = (uint32_t)constant_ranges.size();

      std::array<VkDescriptorSetLayout, 4> compactedLayouts;
      int s = 0;
      for (int i = 0; i < 4; i++)
      {
        if (SetLayouts[i] != VK_NULL_HANDLE)
        {
          compactedLayouts[s] = SetLayouts[i];
          s++;
        }
      }

      mesh_pipeline_layout_info.setLayoutCount = s;
      mesh_pipeline_layout_info.pSetLayouts = compactedLayouts.data();

      vkCreatePipelineLayout(Device, &mesh_pipeline_layout_info, nullptr, &BuiltLayout);
    }

    void FillStages(std::vector<VkPipelineShaderStageCreateInfo>& PipelineStages)
    {

    }

    VkPipelineLayout BuiltLayout;

    struct
    reflected_binding
    {
      u32 Set;
      u32 Binding;
      VkDescriptorType Type;
    };
    
    std::unordered_map<std::string, reflected_binding> Bindings;
    std::array<VkDescriptorSetLayout, 4> SetLayouts;
    std::array<u32, 4> SetHashes;

    struct
    shader_stage
    {
      shader_module* ShaderModule;
      VkShaderStageFlagBits Stage;
    };

    std::vector<shader_stage> Stages;
    
  };

  struct shader_description_binder
  {
    struct buffer_write_descriptor
    {
      u32 DstSet;
      u32 DstBinding;
      VkDescriptorType DescriptorType;
      VkDescriptorBufferInfo BufferInfo;

      u32 DynamicOffset;
    };

    void BindBuffer(const char* Name, const VkDescriptorBufferInfo& BufferInfo)
    {
      BindDinamicBuffer(Name, (u32)-1, BufferInfo);
    }

    void BindDinamicBuffer(const char* Name, u32 Offset, const VkDescriptorBufferInfo& BufferInfo)
    {
      auto found = Shaders->Bindings.find(Name);
      if (found != Shaders->Bindings.end())
      {
        const shader_effect::reflected_binding &bind = (*found).second;

        for (auto &write : BufferWrites)
        {
          if (write.DstBinding == bind.Binding && write.DstSet == bind.Set)
          {
            if (write.BufferInfo.buffer != BufferInfo.buffer ||
                write.BufferInfo.range != BufferInfo.range ||
                write.BufferInfo.offset != BufferInfo.offset)
            {
              write.BufferInfo = BufferInfo;
              write.DynamicOffset = Offset;

              CachedDescriptorSets[write.DstSet] = VK_NULL_HANDLE;
            }
            else
            {
              //already in the write list, but matches buffer
              write.DynamicOffset = Offset;
            }

            return;
          }
        }

        buffer_write_descriptor newWrite;
        newWrite.DstSet = bind.Set;
        newWrite.DstBinding = bind.Binding;
        newWrite.DescriptorType = bind.Type;
        newWrite.BufferInfo = BufferInfo;
        newWrite.DynamicOffset = Offset;

        CachedDescriptorSets[bind.Set] = VK_NULL_HANDLE;

        BufferWrites.push_back(newWrite);
      }
    }

    void ApplyBinds(VkCommandBuffer Cmd)
    {
      for (int i = 0; i < 2; i++)
      {
        //there are writes for this set
        if (CachedDescriptorSets[i] != VK_NULL_HANDLE)
        {

          vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Shaders->BuiltLayout, i, 1, &CachedDescriptorSets[i], SetOffsets[i].Count, SetOffsets[i].Offset.data());
        }
      }
    }

    void BuildSets(VkDevice Device, vk::util::descriptor::allocator& Allocator)
    {

    }
    
    void SetShader(shader_effect* NewShader)
    {

    }

    std::array<VkDescriptorSet, 4> CachedDescriptorSets;

    struct
    dyn_offset
    {
      std::array<u32, 16> Offset;
      u32 Count{0};
    };
    std::array<dyn_offset, 4> SetOffsets;

    shader_effect* Shaders{nullptr};
    std::vector<buffer_write_descriptor> BufferWrites;

  };
  
  struct shader_cache
  {
    shader_module* GetShader(const std::string* Path)
    {

    }

    void Init(VkDevice device) { Device = device; }

    VkDevice Device;
    std::unordered_map<std::string, shader_module> ModuleCache;
  };
  

}