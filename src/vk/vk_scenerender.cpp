
// AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);
#define FREEZE_CULL 0

// AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);
#define SHADOWCAST 1

// AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
#define SHADOW_BIAS 5.25f

// AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);
#define SLOPE_BIAS 4.75f

glm::vec4
NormalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}

// void
// ExecuteComputeCull(VkCommandBuffer cmd,
//                    mesh_pass& pass,
//                    CullParams& params)
// {
// 	if (FREEZE_CULL) return;
	
// 	if (pass.Batches.size() == 0) return;
// 	//TracyVkZone(_graphicsQueueContext, cmd, "Cull Dispatch");
// 	VkDescriptorBufferInfo objectBufferInfo = _renderScene.objectDataBuffer.get_info();

// 	VkDescriptorBufferInfo dynamicInfo = get_current_frame().dynamicData.source.get_info();
// 	dynamicInfo.range = sizeof(GPUCameraData);

// 	VkDescriptorBufferInfo instanceInfo = pass.passObjectsBuffer.get_info();

// 	VkDescriptorBufferInfo finalInfo = pass.compactedInstanceBuffer.get_info();

// 	VkDescriptorBufferInfo indirectInfo = pass.drawIndirectBuffer.get_info();

// 	VkDescriptorImageInfo depthPyramid;
// 	depthPyramid.sampler = _depthSampler;
// 	depthPyramid.imageView = _depthPyramid._defaultView;
// 	depthPyramid.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


// 	VkDescriptorSet COMPObjectDataSet;
// 	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, get_current_frame().dynamicDescriptorAllocator)
// 		.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.bind_buffer(1, &indirectInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.bind_buffer(2, &instanceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.bind_buffer(3, &finalInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.bind_image(4, &depthPyramid, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.bind_buffer(5, &dynamicInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
// 		.build(COMPObjectDataSet);


// 	glm::mat4 projection = params.projmat;
// 	glm::mat4 projectionT = transpose(projection);

// 	glm::vec4 frustumX = NormalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
// 	glm::vec4 frustumY = NormalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

// 	DrawCullData cullData = {};
// 	cullData.P00 = projection[0][0];
// 	cullData.P11 = projection[1][1];
// 	cullData.znear = 0.1f;
// 	cullData.zfar = params.drawDist;
// 	cullData.frustum[0] = frustumX.x;
// 	cullData.frustum[1] = frustumX.z;
// 	cullData.frustum[2] = frustumY.y;
// 	cullData.frustum[3] = frustumY.z;
// 	cullData.drawCount = static_cast<uint32_t>(pass.flat_batches.size());
// 	cullData.cullingEnabled = params.frustrumCull;
// 	cullData.lodEnabled = false;
// 	cullData.occlusionEnabled = params.occlusionCull;
// 	cullData.lodBase = 10.f;
// 	cullData.lodStep = 1.5f;
// 	cullData.pyramidWidth = static_cast<float>(depthPyramidWidth);
// 	cullData.pyramidHeight = static_cast<float>(depthPyramidHeight);
// 	cullData.viewMat = params.viewmat;//get_view_matrix();

// 	cullData.AABBcheck = params.aabb;
// 	cullData.aabbmin_x = params.aabbmin.x;
// 	cullData.aabbmin_y = params.aabbmin.y;
// 	cullData.aabbmin_z = params.aabbmin.z;

// 	cullData.aabbmax_x = params.aabbmax.x;
// 	cullData.aabbmax_y = params.aabbmax.y;
// 	cullData.aabbmax_z = params.aabbmax.z;

// 	if (params.drawDist > 10000)
// 	{
// 		cullData.distanceCheck = false; 
// 	}
// 	else
// 	{
// 		cullData.distanceCheck = true;
// 	}

// 	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullPipeline);

// 	vkCmdPushConstants(cmd, _cullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DrawCullData), &cullData);

// 	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cullLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);
	
// 	vkCmdDispatch(cmd, static_cast<uint32_t>((pass.flat_batches.size() / 256)+1), 1, 1);


// 	//barrier the 2 buffers we just wrote for culling, the indirect draw one, and the instances one, so that they can be read well when rendering the pass
// 	{
// 		VkBufferMemoryBarrier barrier = vkinit::buffer_barrier(pass.compactedInstanceBuffer._buffer, _graphicsQueueFamily);
// 		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
// 		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

// 		VkBufferMemoryBarrier barrier2 = vkinit::buffer_barrier(pass.drawIndirectBuffer._buffer, _graphicsQueueFamily);	
// 		barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
// 		barrier2.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

// 		VkBufferMemoryBarrier barriers[] = { barrier,barrier2 };

// 		postCullBarriers.push_back(barrier);
// 		postCullBarriers.push_back(barrier2);
// 	}
// 	if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
// 	{
// 		uint32_t offset = get_current_frame().debugDataOffsets.back();
// 		VkBufferCopy debugCopy;
// 		debugCopy.dstOffset = offset;
// 		debugCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
// 		debugCopy.srcOffset = 0;
// 		vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer._buffer, get_current_frame().debugOutputBuffer._buffer, 1, &debugCopy);
// 		get_current_frame().debugDataOffsets.push_back(offset + static_cast<uint32_t>(debugCopy.size));
// 		get_current_frame().debugDataNames.push_back("Cull Indirect Output");
// 	}
// }