
//namespace vk::material_system
//{

struct pipeline_builder
{
  VkPipeline
  BuildPipeline(VkRenderPass pass)
  {
    VertexInputInfo = vk::init::vertex_input_state_create_info();
    //connect the pipeline builder vertex input info to the one we get from Vertex
    VertexInputInfo.pVertexAttributeDescriptions = VertexDescription.Attributes.data();
    VertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)VertexDescription.Attributes.size();

    VertexInputInfo.pVertexBindingDescriptions = VertexDescription.Bindings.data();
    VertexInputInfo.vertexBindingDescriptionCount = (uint32_t)VertexDescription.Bindings.size();

    //make viewport state from our stored viewport and scissor.
    //at the moment we wont support multiple viewports or scissors
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;

    viewportState.viewportCount = 1;
    viewportState.pViewports = &Viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &Scissor;

    //setup dummy color blending. We arent using transparent objects yet
    //the blending is just "no blend", but we do write to the color attachment
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &ColorBlendAttachment;

    //build the actual pipeline
    //we now use all of the info structs we have been writing into into this one to create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;

    pipelineInfo.stageCount = (u32)ShaderStages.size();
    pipelineInfo.pStages = ShaderStages.data();
    pipelineInfo.pVertexInputState = &VertexInputInfo;
    pipelineInfo.pInputAssemblyState = &InputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &Rasterizer;
    pipelineInfo.pMultisampleState = &Multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &DepthStencil;
    pipelineInfo.layout = PipelineLayout;
    pipelineInfo.renderPass = pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

    std::vector<VkDynamicState> dynamicStates;
    dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    dynamicState.pDynamicStates = dynamicStates.data();
    dynamicState.dynamicStateCount = (u32)dynamicStates.size();

    pipelineInfo.pDynamicState = &dynamicState;

    //its easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(vk::renderer::g_PlatformState.Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
    {
      //LOG_FATAL("Failed to build graphics pipeline");
      return VK_NULL_HANDLE;
    }
    else
    {
      return newPipeline;
    }
  }

  void
  ClearVertexInput()
  {
    VertexInputInfo.pVertexAttributeDescriptions = nullptr;
    VertexInputInfo.vertexAttributeDescriptionCount = 0;

    VertexInputInfo.pVertexBindingDescriptions = nullptr;
    VertexInputInfo.vertexBindingDescriptionCount = 0;
  }

  void
  SetShaders(vk::shader::shader_effect* effect)
  {
    ShaderStages.clear();
    effect->FillStages(ShaderStages);

    PipelineLayout = effect->BuiltLayout;
  }

  std::vector<VkPipelineShaderStageCreateInfo> ShaderStages;
  VertexInputDescription VertexDescription;
  VkPipelineVertexInputStateCreateInfo VertexInputInfo;
  VkPipelineInputAssemblyStateCreateInfo InputAssembly;
  VkViewport Viewport;
  VkRect2D Scissor;
  VkPipelineRasterizationStateCreateInfo Rasterizer;
  VkPipelineColorBlendAttachmentState ColorBlendAttachment;
  VkPipelineMultisampleStateCreateInfo Multisampling;
  VkPipelineLayout PipelineLayout;
  VkPipelineDepthStencilStateCreateInfo DepthStencil;
};

enum class VertexAttributeTemplate
{
	DefaultVertex,
	DefaultVertexPosOnly
};

struct effect_builder
{
	VertexAttributeTemplate VertexAttrib;
	vk::shader::shader_effect* Effect{ nullptr };

	VkPrimitiveTopology Topology;
	VkPipelineRasterizationStateCreateInfo RasterizerInfo;
	VkPipelineColorBlendAttachmentState ColorBlendAttachmentInfo;
	VkPipelineDepthStencilStateCreateInfo DepthStencilInfo;
};

struct compute_pipeline_builder
{
  VkPipelineShaderStageCreateInfo ShaderStage;
  VkPipelineLayout PipelineLayout;

  VkPipeline
  BuildPipeline()
  {
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;

    pipelineInfo.stage = ShaderStage;
    pipelineInfo.layout = PipelineLayout;

    VkPipeline newPipeline;
    if (vkCreateComputePipelines(vk::renderer::g_PlatformState.Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
    {
      //LOG_FATAL("Failed to build compute pipeline");
      return VK_NULL_HANDLE;
    }
    else
    {
      return newPipeline;
    }
  }
};

//} // namespace vk::material_system

namespace vk::util
{

struct
shader_pass
{
  vk::shader::shader_effect* Effect {nullptr};
  VkPipeline Pipeline {VK_NULL_HANDLE};
  VkPipelineLayout Layout{VK_NULL_HANDLE};
};

struct
sampled_texture
{
  VkSampler Sampler;
  VkImageView View;
};

struct
shader_parametrs
{
  
};

template<typename T>
struct
per_pass_data
{
  public:

  T&
  operator[](MeshpassType pass)
  {
    switch (pass)
    {
    case MeshpassType::Forward:
      return Data[0];
    case MeshpassType::Transparency:
      return Data[1];
    case MeshpassType::DirectionalShadow:
      return Data[2];
    }
    Assert(false);
    return Data[0];
  };

  void
  Clear(T &&val)
  {
    for (int i = 0; i < 3; i++)
    {
      Data[i] = val;
    }
  }

  private:

	std::array<T, 3> Data;
};

struct
effect_template
{
		per_pass_data<shader_pass*> PassShaders;
		
		shader_parametrs* DefaultParameters;

		// assets::TransparencyMode Transparency;
};

struct
material_data
{
  std::vector<sampled_texture> Textures;
	shader_parametrs* Parameters;
	std::string BaseTemplate;

  bool
  operator==(const material_data &other) const
  {
    if (other.BaseTemplate.compare(BaseTemplate) != 0 || other.Parameters != Parameters || other.Textures.size() != Textures.size())
    {
      return false;
    }
    else
    {
      //binary compare textures
      bool comp = memcmp(other.Textures.data(), Textures.data(), Textures.size() * sizeof(Textures[0])) == 0;
      return comp;
    }
  }

  u64
  Hash() const
  {
    using std::hash;

    u64 result = hash<std::string>()(BaseTemplate);

    for (const auto &b : Textures)
    {
      //pack the binding data into a single int64. Not fully correct but its ok
      u64 texture_hash = (std::hash<u64>()((u64)b.Sampler) << 3) && (std::hash<u64>()((u64)b.View) >> 7);

      //shuffle the packed binding data and xor it with the main hash
      result ^= std::hash<u64>()(texture_hash);
    }

    return result;
  }
};

struct
material
{
	effect_template* Original;
	per_pass_data<VkDescriptorSet> PassSets;
		
	std::vector<sampled_texture> Textures;

	shader_parametrs* Parameters;

	material&
  operator=(const material& other) = default;
};

struct
material_system
{
public:

vk::shader::shader_effect*
BuildEffect(vk::shader::shader_cache* shaderCache, std::string_view vertexShader, std::string_view fragmentShader)
{
  vk::shader::shader_effect::reflection_overrides overrides[] =
  {
		{"sceneData", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
		{"cameraData", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC}
	};
	//textured defaultlit shader
	vk::shader::shader_effect* effect = new vk::shader::shader_effect();
	
	effect->AddStage(shaderCache->GetShader(vk::renderer::ShaderPath(vertexShader)), VK_SHADER_STAGE_VERTEX_BIT);
	if (fragmentShader.size() > 2)
	{
		effect->AddStage(shaderCache->GetShader(vk::renderer::ShaderPath(fragmentShader)), VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	

	effect->ReflectLayout(overrides, 2);

	return effect; 
}

void
build_default_templates(vk::shader::shader_cache* shaderCache, VkRenderPass renderPass, VkRenderPass shadowPass)
{
  FillBuilders();

  //default effects	
	vk::shader::shader_effect* texturedLit = BuildEffect(shaderCache,  "tri_mesh_ssbo_instanced.vert.spv" ,"textured_lit.frag.spv" );
	vk::shader::shader_effect* defaultLit = BuildEffect(shaderCache, "tri_mesh_ssbo_instanced.vert.spv" , "default_lit.frag.spv" );
	vk::shader::shader_effect* opaqueShadowcast = BuildEffect(shaderCache, "tri_mesh_ssbo_instanced_shadowcast.vert.spv","");

	//passes
	shader_pass* texturedLitPass = BuildShader(renderPass, ForwardBuilder, texturedLit);
	shader_pass* defaultLitPass = BuildShader(renderPass, ForwardBuilder, defaultLit);
	shader_pass* opaqueShadowcastPass = BuildShader(shadowPass, ShadowBuilder, opaqueShadowcast);


	{
		effect_template defaultTextured;
		defaultTextured.PassShaders[MeshpassType::Transparency] = nullptr;
		defaultTextured.PassShaders[MeshpassType::DirectionalShadow] = opaqueShadowcastPass;
		defaultTextured.PassShaders[MeshpassType::Forward] = texturedLitPass;

		defaultTextured.DefaultParameters = nullptr;
		// defaultTextured.Transparency = assets::TransparencyMode::Opaque;

		TemplateCache["texturedPBR_opaque"] = defaultTextured;
	}
	{
		pipeline_builder transparentForward = ForwardBuilder;

		transparentForward.ColorBlendAttachment.blendEnable = VK_TRUE;
		transparentForward.ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		transparentForward.ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		transparentForward.ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;


		//transparentForward._colorBlendAttachment.colorBlendOp = VK_BLEND_OP_OVERLAY_EXT;
		transparentForward.ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
		
		transparentForward.DepthStencil.depthWriteEnable = false;

		transparentForward.Rasterizer.cullMode = VK_CULL_MODE_NONE;
		//passes
		shader_pass* transparentLitPass = BuildShader(renderPass, transparentForward, texturedLit);

		effect_template defaultTextured;
		defaultTextured.PassShaders[MeshpassType::Transparency] = transparentLitPass;
		defaultTextured.PassShaders[MeshpassType::DirectionalShadow] = nullptr;
		defaultTextured.PassShaders[MeshpassType::Forward] = nullptr;

		defaultTextured.DefaultParameters = nullptr;
		// defaultTextured.Transparency = assets::TransparencyMode::Transparent;

		TemplateCache["texturedPBR_transparent"] = defaultTextured;
	}

	{
		effect_template defaultColored;
		
		defaultColored.PassShaders[MeshpassType::Transparency] = nullptr;
		defaultColored.PassShaders[MeshpassType::DirectionalShadow] = opaqueShadowcastPass;
		defaultColored.PassShaders[MeshpassType::Forward] = defaultLitPass;
		defaultColored.DefaultParameters = nullptr;
		// defaultColored.Transparency = assets::TransparencyMode::Opaque;
		TemplateCache["colored_opaque"] = defaultColored;
	}
}

shader_pass* BuildShader(VkRenderPass renderPass, pipeline_builder& builder, vk::shader::shader_effect* effect)
{
	shader_pass* pass = new shader_pass();

	pass->Effect = effect;
	pass->Layout = effect->BuiltLayout;

	pipeline_builder pipbuilder = builder;

	pipbuilder.SetShaders(effect);

	pass->Pipeline = pipbuilder.BuildPipeline(renderPass);

	return pass;
}

//private:

void FillBuilders()
{
	{
		ShadowBuilder.VertexDescription = Vertex::get_vertex_description();

		ShadowBuilder.InputAssembly = vk::init::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		
		ShadowBuilder.Rasterizer = vk::init::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		ShadowBuilder.Rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
		ShadowBuilder.Rasterizer.depthBiasEnable = VK_TRUE;
	
	  ShadowBuilder.Multisampling = vk::init::multisampling_state_create_info();
		ShadowBuilder.ColorBlendAttachment = vk::init::color_blend_attachment_state();

		//default depthtesting
		ShadowBuilder.DepthStencil = vk::init::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS);
	}
	{
		ForwardBuilder.VertexDescription = Vertex::get_vertex_description();
		
		ForwardBuilder.InputAssembly = vk::init::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		
		ForwardBuilder.Rasterizer = vk::init::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		ForwardBuilder.Rasterizer.cullMode = VK_CULL_MODE_NONE;//BACK_BIT;
		
		ForwardBuilder.Multisampling = vk::init::multisampling_state_create_info();
		
		ForwardBuilder.ColorBlendAttachment = vk::init::color_blend_attachment_state();

		//default depthtesting
		ForwardBuilder.DepthStencil = vk::init::depth_stencil_create_info(true, true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	}	
}

material*
BuildMaterial(const std::string& materialName,
              const material_data& info,
              vk::descriptor::cache& descriptorLayoutCache,
              vk::descriptor::allocator& descriptorAllocator)
{
	material* mat;
	//search material in the cache first in case its already built
	auto it = MaterialCache.find(info);
	if (it != MaterialCache.end())
	{
		mat = (*it).second;
		Materials[materialName] = mat;
	}
	else {

		//need to build the material
		material *newMat = new material();
		newMat->Original = &TemplateCache[ info.BaseTemplate];
		newMat->Parameters = info.Parameters;
		//not handled yet
		newMat->PassSets[MeshpassType::DirectionalShadow] = VK_NULL_HANDLE;
		newMat->Textures = info.Textures;

    std::vector<vk::descriptor::builder::descriptor_bind> binds;		

		for (u32 i = 0; i < info.Textures.size(); i++)
		{
			VkDescriptorImageInfo imageBufferInfo;
			imageBufferInfo.sampler = info.Textures[i].Sampler;
			imageBufferInfo.imageView = info.Textures[i].View;
			imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // VkDescriptorBufferInfo

      vk::descriptor::builder::descriptor_bind bind = { i, nullptr, &imageBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT };
      binds.push_back( bind );
		}

		vk::descriptor::builder::BuildDescriptorSet(descriptorAllocator, descriptorLayoutCache, newMat->PassSets[MeshpassType::Forward], binds);
    vk::descriptor::builder::BuildDescriptorSet(descriptorAllocator, descriptorLayoutCache, newMat->PassSets[MeshpassType::Transparency], binds);

		//LOG_INFO("Built New Material {}", materialName);
		//add material to cache
		MaterialCache[info] = (newMat);
		mat = newMat;
		Materials[materialName] = mat;
	}

	return mat;
}

material*
GetMaterial(const std::string& materialName)
{
	auto it = Materials.find(materialName);
	if (it != Materials.end())
	{
		return(*it).second;
	}
	else
  {
		return nullptr;
	}
}

struct MaterialInfoHash
{

  u64
  operator()(const material_data &k) const
  {
    return k.Hash();
  }
};

pipeline_builder ForwardBuilder;
pipeline_builder ShadowBuilder;

std::unordered_map<std::string, effect_template> TemplateCache;
std::unordered_map<std::string, material *> Materials;
std::unordered_map<material_data, material *, MaterialInfoHash> MaterialCache;

};


}

