
#include <future>

// AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);
#define FREEZE_CULL 0

// AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);
#define SHADOWCAST 0

// AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
#define SHADOW_BIAS 5.25f

// AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);
#define SLOPE_BIAS 4.75f

namespace vk::renderer
{

glm::vec4
NormalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}

void
ExecuteComputeCull(VkCommandBuffer cmd,
                   render_scene::mesh_pass& pass,
                   CullParams& params)
{
	if (FREEZE_CULL) return;
	
	if (pass.Batches.size() == 0) return;
	//TracyVkZone(_graphicsQueueContext, cmd, "Cull Dispatch");
	VkDescriptorBufferInfo objectBufferInfo = g_RenderState.RenderScene->ObjectDataBuffer.GetInfo();

	VkDescriptorBufferInfo dynamicInfo = g_RenderState.GetCurrentFrame().DynamicData.Source.GetInfo();
	dynamicInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.PassObjectsBuffer.GetInfo();

	VkDescriptorBufferInfo finalInfo = pass.CompactedInstanceBuffer.GetInfo();

	VkDescriptorBufferInfo indirectInfo = pass.DrawIndirectBuffer.GetInfo();

	VkDescriptorImageInfo depthPyramid;
	depthPyramid.sampler = g_RenderState.DepthSampler;
	depthPyramid.imageView = g_RenderState.DepthPyramid.DefaultView;
	depthPyramid.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorSet COMPObjectDataSet;

	vk::descriptor::builder::descriptor_bind binds[] =
	{
		{ 0, &objectBufferInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 1, &indirectInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 2, &instanceInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 3, &finalInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 4, nullptr, &depthPyramid, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
		{ 5, &dynamicInfo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(
		*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator,
		*g_RenderState.DescriptorLayoutCache, COMPObjectDataSet, binds, 6);

	glm::mat4 projection = params.projmat;
	glm::mat4 projectionT = transpose(projection);

	glm::vec4 frustumX = NormalizePlane(projectionT[3] + projectionT[0]); // x + w < 0
	glm::vec4 frustumY = NormalizePlane(projectionT[3] + projectionT[1]); // y + w < 0

	DrawCullData cullData = {};
	cullData.P00 = projection[0][0];
	cullData.P11 = projection[1][1];
	cullData.znear = 0.1f;
	cullData.zfar = params.drawDist;
	cullData.frustum[0] = frustumX.x;
	cullData.frustum[1] = frustumX.z;
	cullData.frustum[2] = frustumY.y;
	cullData.frustum[3] = frustumY.z;
	cullData.drawCount = static_cast<u32>(pass.FlatBatches.size());
	cullData.cullingEnabled = params.frustrumCull;
	cullData.lodEnabled = false;
	cullData.occlusionEnabled = params.occlusionCull;
	cullData.lodBase = 10.f;
	cullData.lodStep = 1.5f;
	cullData.pyramidWidth = static_cast<f32>(g_RenderState.DepthPyramidWidth);
	cullData.pyramidHeight = static_cast<f32>(g_RenderState.DepthPyramidHeight);
	cullData.viewMat = params.viewmat;//get_view_matrix();

	cullData.AABBcheck = params.aabb;
	cullData.aabbmin_x = params.aabbmin.x;
	cullData.aabbmin_y = params.aabbmin.y;
	cullData.aabbmin_z = params.aabbmin.z;

	cullData.aabbmax_x = params.aabbmax.x;
	cullData.aabbmax_y = params.aabbmax.y;
	cullData.aabbmax_z = params.aabbmax.z;

	if (params.drawDist > 10000)
	{
		cullData.distanceCheck = false; 
	}
	else
	{
		cullData.distanceCheck = true;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.CullPipeline);

	vkCmdPushConstants(cmd, g_RenderState.CullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DrawCullData), &cullData);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.CullLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);
	
	vkCmdDispatch(cmd, static_cast<uint32_t>((pass.FlatBatches.size() / 256)+1), 1, 1);


	//barrier the 2 buffers we just wrote for culling, the indirect draw one, and the instances one, so that they can be read well when rendering the pass
	{
		VkBufferMemoryBarrier barrier = vk::init::buffer_barrier(pass.CompactedInstanceBuffer.Buffer, g_RenderState.GraphicsQueueFamily);
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		VkBufferMemoryBarrier barrier2 = vk::init::buffer_barrier(pass.DrawIndirectBuffer.Buffer, g_RenderState.GraphicsQueueFamily);	
		barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier2.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

		VkBufferMemoryBarrier barriers[] = { barrier, barrier2 };

		g_RenderState.PostCullBarriers.push_back(barrier);
		g_RenderState.PostCullBarriers.push_back(barrier2);
	}
	// if (*CVarSystem::Get()->GetIntCVar("culling.outputIndirectBufferToFile"))
	// {
	// 	uint32_t offset = get_current_frame().debugDataOffsets.back();
	// 	VkBufferCopy debugCopy;
	// 	debugCopy.dstOffset = offset;
	// 	debugCopy.size = pass.batches.size() * sizeof(GPUIndirectObject);
	// 	debugCopy.srcOffset = 0;
	// 	vkCmdCopyBuffer(cmd, pass.drawIndirectBuffer._buffer, get_current_frame().debugOutputBuffer._buffer, 1, &debugCopy);
	// 	get_current_frame().debugDataOffsets.push_back(offset + static_cast<uint32_t>(debugCopy.size));
	// 	get_current_frame().debugDataNames.push_back("Cull Indirect Output");
	// }
}

void
ReadyMeshDraw(VkCommandBuffer cmd)
{
	// TracyVkZone(_graphicsQueueContext, get_current_frame()._mainCommandBuffer, "Data Refresh");
	// ZoneScopedNC("Draw Upload", tracy::Color::Blue);

	//upload object data to gpu

	if (g_RenderState.RenderScene->DirtyObjects.size() > 0)
	{
		// ZoneScopedNC("Refresh Object Buffer", tracy::Color::Red);

		size_t copySize = g_RenderState.RenderScene->Renderables.size() *
											sizeof(GPUObjectData);
		if (g_RenderState.RenderScene->ObjectDataBuffer.Size < copySize)
		{
			ReallocateBuffer(g_RenderState.RenderScene->ObjectDataBuffer,
											 copySize,
											 VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
											 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
											 VMA_MEMORY_USAGE_CPU_TO_GPU);
		}

		//if 80% of the objects are dirty, then just reupload the whole thing
		if (g_RenderState.RenderScene->DirtyObjects.size() >= g_RenderState.RenderScene->Renderables.size() * 0.8)
		{
			allocated_buffer<GPUObjectData> newBuffer = CreateBuffer(copySize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			GPUObjectData *objectSSBO = MapBuffer(newBuffer);
			g_RenderState.RenderScene->FillObjectData(objectSSBO);
			UnmapBuffer(newBuffer);

			g_RenderState.GetCurrentFrame().FrameDeletionQueue.push_function([=]()
			{ vmaDestroyBuffer(g_Allocator, newBuffer.Buffer, newBuffer.Allocation); });

			//copy from the uploaded cpu side instance buffer to the gpu one
			VkBufferCopy indirectCopy;
			indirectCopy.dstOffset = 0;
			indirectCopy.size = g_RenderState.RenderScene->Renderables.size() * sizeof(GPUObjectData);
			indirectCopy.srcOffset = 0;
			vkCmdCopyBuffer(cmd, newBuffer.Buffer, g_RenderState.RenderScene->ObjectDataBuffer.Buffer, 1, &indirectCopy);
		}
		else
		{
			//update only the changed elements

			std::vector<VkBufferCopy> copies;
			copies.reserve(g_RenderState.RenderScene->DirtyObjects.size());

			u64 buffersize = sizeof(GPUObjectData) * g_RenderState.RenderScene->DirtyObjects.size();
			u64 vec4size = sizeof(glm::vec4);
			u64 intsize = sizeof(u32);
			u64 wordsize = sizeof(GPUObjectData) / sizeof(u32);
			u64 uploadSize = g_RenderState.RenderScene->DirtyObjects.size() * wordsize * intsize;
			allocated_buffer<GPUObjectData> newBuffer = CreateBuffer(buffersize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
			allocated_buffer<u32> targetBuffer = CreateBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

			g_RenderState.GetCurrentFrame().FrameDeletionQueue.push_function([=]()
			{
				vmaDestroyBuffer(g_Allocator, newBuffer.Buffer, newBuffer.Allocation);
			  vmaDestroyBuffer(g_Allocator, targetBuffer.Buffer, targetBuffer.Allocation);
			});

			u32 *targetData = MapBuffer(targetBuffer);
			GPUObjectData *objectSSBO = MapBuffer(newBuffer);
			u32 launchcount = static_cast<u32>(g_RenderState.RenderScene->DirtyObjects.size() * wordsize);
			{
				// ZoneScopedNC("Write dirty objects", tracy::Color::Red);
				u32 sidx = 0;
				for (i32 i = 0; i < g_RenderState.RenderScene->DirtyObjects.size(); i++)
				{
					g_RenderState.RenderScene->WriteObject(objectSSBO + i, g_RenderState.RenderScene->DirtyObjects[i]);

					u32 dstOffset = static_cast<u32>(wordsize * g_RenderState.RenderScene->DirtyObjects[i].Handle);

					for (i32 b = 0; b < wordsize; b++)
					{
						u32 tidx = dstOffset + b;
						targetData[sidx] = tidx;
						sidx++;
					}
				}
				launchcount = sidx;
			}
			UnmapBuffer(newBuffer);
			UnmapBuffer(targetBuffer);

			VkDescriptorBufferInfo indexData = targetBuffer.GetInfo();

			VkDescriptorBufferInfo sourceData = newBuffer.GetInfo();

			VkDescriptorBufferInfo targetInfo = g_RenderState.RenderScene->ObjectDataBuffer.GetInfo();

			VkDescriptorSet COMPObjectDataSet;

			vk::descriptor::builder::descriptor_bind binds[] =
			{
				{ 0, &indexData, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
				{ 1, &sourceData, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT },
				{ 2, &targetInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT }
			};

			vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, COMPObjectDataSet, binds, 3);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.SparseUploadPipeline);

			vkCmdPushConstants(cmd, g_RenderState.SparseUploadLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u32), &launchcount);

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.SparseUploadLayout, 0, 1, &COMPObjectDataSet, 0, nullptr);

			vkCmdDispatch(cmd, ((launchcount) / 256) + 1, 1, 1);
		}

		VkBufferMemoryBarrier barrier = vk::init::buffer_barrier(g_RenderState.RenderScene->ObjectDataBuffer.Buffer, g_RenderState.GraphicsQueueFamily);
		barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		g_RenderState.UploadBarriers.push_back(barrier);
		g_RenderState.RenderScene->ClearDirtyObjects();
	}

	render_scene::mesh_pass *passes[3] = {&g_RenderState.RenderScene->ForwardPass, &g_RenderState.RenderScene->TransparentForwardPass, &g_RenderState.RenderScene->ShadowPass};
	for (int p = 0; p < 3; p++)
	{
		auto &pass = *passes[p];

		//reallocate the gpu side buffers if needed

		if (pass.DrawIndirectBuffer.Size < pass.Batches.size() * sizeof(gpu_indirect_object))
		{
			ReallocateBuffer(pass.DrawIndirectBuffer, pass.Batches.size() * sizeof(gpu_indirect_object), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}

		if (pass.CompactedInstanceBuffer.Size < pass.FlatBatches.size() * sizeof(u32))
		{
			ReallocateBuffer(pass.CompactedInstanceBuffer, pass.FlatBatches.size() * sizeof(u32), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}

		if (pass.PassObjectsBuffer.Size < pass.FlatBatches.size() * sizeof(gpu_instance))
		{
			ReallocateBuffer(pass.PassObjectsBuffer, pass.FlatBatches.size() * sizeof(gpu_instance), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
		}
	}

	std::vector<std::future<void>> async_calls;
	async_calls.reserve(9);

	std::vector<allocated_buffer_untyped> unmaps;

	for (i32 p = 0; p < 3; p++)
	{
		render_scene::mesh_pass &pass = *passes[p];
		render_scene::mesh_pass *ppass = passes[p];

		render_scene *pScene = g_RenderState.RenderScene;
		//if the pass has changed the batches, need to reupload them
		if (pass.NeedsIndirectRefresh && pass.Batches.size() > 0)
		{
			//ZoneScopedNC("Refresh Indirect Buffer", tracy::Color::Red);

			allocated_buffer<gpu_indirect_object> newBuffer =
				CreateBuffer(sizeof(gpu_indirect_object) * pass.Batches.size(),
										 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | 
										 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
										 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
										 VMA_MEMORY_USAGE_CPU_TO_GPU);

			gpu_indirect_object *indirect = MapBuffer(newBuffer);

			async_calls.push_back(std::async(std::launch::async, [=]
			{
			  pScene->FillIndirectArray(indirect, *ppass);
		 	}));
			//async_calls.push_back([&]() {
			//	g_RenderState.RenderScene->fill_indirectArray(indirect, pass);
			//});

			unmaps.push_back(newBuffer);

			//unMapBuffer(newBuffer);

			if (pass.ClearIndirectBuffer.Buffer != VK_NULL_HANDLE)
			{
				allocated_buffer_untyped deletionBuffer = pass.ClearIndirectBuffer;
				//add buffer to deletion queue of this frame
				g_RenderState.GetCurrentFrame().FrameDeletionQueue.push_function([=]()
				{vmaDestroyBuffer(g_Allocator,deletionBuffer.Buffer,deletionBuffer.Allocation);});
			}

			pass.ClearIndirectBuffer = newBuffer;
			pass.NeedsIndirectRefresh = false;
		}

		if (pass.NeedsInstanceRefresh && pass.FlatBatches.size() > 0)
		{
			// ZoneScopedNC("Refresh Instancing Buffer", tracy::Color::Red);

			allocated_buffer<gpu_instance> newBuffer =
				CreateBuffer(sizeof(gpu_instance) * pass.FlatBatches.size(),
										 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | 
										 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
										 VMA_MEMORY_USAGE_CPU_TO_GPU);

			gpu_instance* instanceData = MapBuffer(newBuffer);
			async_calls.push_back(std::async(std::launch::async, [=]
			{
			  pScene->FillInstancesArray(instanceData, *ppass);
			}));
			unmaps.push_back(newBuffer);
			//g_RenderState.RenderScene->fill_instancesArray(instanceData, pass);
			//unMapBuffer(newBuffer);

			g_RenderState.GetCurrentFrame().FrameDeletionQueue.push_function([=]()
			{vmaDestroyBuffer(g_Allocator,newBuffer.Buffer,newBuffer.Allocation);});

			//copy from the uploaded cpu side instance buffer to the gpu one
			VkBufferCopy indirectCopy;
			indirectCopy.dstOffset = 0;
			indirectCopy.size = pass.FlatBatches.size() * sizeof(gpu_instance);
			indirectCopy.srcOffset = 0;
			vkCmdCopyBuffer(cmd, newBuffer.Buffer,
											pass.PassObjectsBuffer.Buffer, 1, &indirectCopy);

			VkBufferMemoryBarrier barrier =
				vk::init::buffer_barrier(pass.PassObjectsBuffer.Buffer,
																 g_RenderState.GraphicsQueueFamily);
			barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | 
															VK_ACCESS_SHADER_READ_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			g_RenderState.UploadBarriers.push_back(barrier);

			pass.NeedsInstanceRefresh = false;
		}
	}

	for (auto &s : async_calls)
	{
		s.get();
	}

	for (auto b : unmaps)
	{
		UnmapBuffer(b);
	}

	vkCmdPipelineBarrier(cmd,
											 VK_PIPELINE_STAGE_TRANSFER_BIT,
											 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
											 0, 0, nullptr,
											 static_cast<u32>(g_RenderState.UploadBarriers.size()),
											 g_RenderState.UploadBarriers.data(), 0, nullptr); //1, &readBarrier);
	g_RenderState.UploadBarriers.clear();
}

void
ExecuteDrawCommands(VkCommandBuffer cmd,
										render_scene::mesh_pass& pass,
										VkDescriptorSet ObjectDataSet,
										std::vector<u32> dynamic_offsets,
										VkDescriptorSet GlobalSet)
{
	if(pass.Batches.size() > 0)
	{
		// ZoneScopedNC("Draw Commit", tracy::Color::Blue4);
		Mesh *lastMesh = nullptr;
		VkPipeline lastPipeline{VK_NULL_HANDLE};
		VkPipelineLayout lastLayout{VK_NULL_HANDLE};
		VkDescriptorSet lastMaterialSet{VK_NULL_HANDLE};

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &g_RenderState.RenderScene->MergedVertexBuffer.Buffer, &offset);

		if (g_RenderState.RenderScene->MergedIndexBuffer.Buffer)
			vkCmdBindIndexBuffer(cmd, g_RenderState.RenderScene->MergedIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

		g_RenderState.stats.objects = static_cast<u32>(pass.FlatBatches.size());
		for (i32 i = 0; i < pass.Multibatches.size(); i++)
		{
			auto &multibatch = pass.Multibatches[i];
			auto &instanceDraw = pass.Batches[multibatch.First];

			VkPipeline newPipeline = instanceDraw.Material.ShaderPass->Pipeline;
			VkPipelineLayout newLayout = instanceDraw.Material.ShaderPass->Layout;
			VkDescriptorSet newMaterialSet = instanceDraw.Material.MaterialSet;

			Mesh *drawMesh = g_RenderState.RenderScene->GetMesh(instanceDraw.MeshID)->Original;

			if (newPipeline != lastPipeline)
			{
				lastPipeline = newPipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 1, 1, &ObjectDataSet, 0, nullptr);

				//update dynamic binds
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 0, 1, &GlobalSet, (u32)dynamic_offsets.size(), dynamic_offsets.data());
			}
			if (newMaterialSet != lastMaterialSet)
			{
				lastMaterialSet = newMaterialSet;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, newLayout, 2, 1, &newMaterialSet, 0, nullptr);
			}

			bool merged = g_RenderState.RenderScene->GetMesh(instanceDraw.MeshID)->IsMerged;
			if (merged)
			{
				if (lastMesh != nullptr)
				{
					offset = 0;
					vkCmdBindVertexBuffers(cmd, 0, 1, &g_RenderState.RenderScene->MergedVertexBuffer.Buffer, &offset);

					vkCmdBindIndexBuffer(cmd, g_RenderState.RenderScene->MergedIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
					lastMesh = nullptr;
				}
			}
			else if (lastMesh != drawMesh)
			{
				//bind the mesh vertex buffer with offset 0
				offset = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &drawMesh->VertexBuffer.Buffer, &offset);

				if (drawMesh->IndexBuffer.Buffer != VK_NULL_HANDLE)
				{
					vkCmdBindIndexBuffer(cmd, drawMesh->IndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);
				}
				lastMesh = drawMesh;
			}

			bool bHasIndices = drawMesh->Indices.size() > 0;
			if (!bHasIndices)
			{
				g_RenderState.stats.draws++;
				g_RenderState.stats.triangles += static_cast<i32>(drawMesh->Vertices.size() / 3) * instanceDraw.Count;
				vkCmdDraw(cmd, static_cast<u32>(drawMesh->Vertices.size()), instanceDraw.Count, 0, instanceDraw.First);
			}
			else
			{
				g_RenderState.stats.triangles += static_cast<i32>(drawMesh->Indices.size() / 3) * instanceDraw.Count;

				vkCmdDrawIndexedIndirect(cmd, pass.DrawIndirectBuffer.Buffer, multibatch.First * sizeof(gpu_indirect_object), multibatch.Count, sizeof(gpu_indirect_object));

				g_RenderState.stats.draws++;
				g_RenderState.stats.drawcalls += instanceDraw.Count;
			}
		}
	}
}

void
DrawObjectsForward(VkCommandBuffer cmd,
									 render_scene::mesh_pass &pass)
{
	// ZoneScopedNC("DrawObjects", tracy::Color::Blue);
	//make a model view matrix for rendering the object
	//camera view
	glm::mat4 view = g_RenderState.Camera.GetViewMatrix();
	//camera projection
	glm::mat4 projection = g_RenderState.Camera.GetProjectionMatrix();

	GPUCameraData camData;
	camData.Projection = projection;
	camData.View = view;
	camData.Viewproj = projection * view;

	g_RenderState.SceneParameters.SunlightShadowMatrix = g_RenderState.MainLight.get_projection() * g_RenderState.MainLight.get_view();

	f32 framed = (g_RenderState.FrameNumber / 120.f);
	g_RenderState.SceneParameters.AmbientColor = glm::vec4{0.5};
	g_RenderState.SceneParameters.SunlightColor = glm::vec4{1.f};
	g_RenderState.SceneParameters.SunlightDirection = glm::vec4(g_RenderState.MainLight.lightDirection * 1.f, 1.f);

	g_RenderState.SceneParameters.SunlightColor.w = SHADOWCAST ? 0 : 1;

	//push data to dynmem
	u32 scene_data_offset = g_RenderState.GetCurrentFrame().DynamicData.Push(g_RenderState.SceneParameters);

	uint32_t camera_data_offset = g_RenderState.GetCurrentFrame().DynamicData.Push(camData);

	VkDescriptorBufferInfo objectBufferInfo = g_RenderState.RenderScene->ObjectDataBuffer.GetInfo();

	VkDescriptorBufferInfo sceneInfo = g_RenderState.GetCurrentFrame().DynamicData.Source.GetInfo();
	sceneInfo.range = sizeof(GPUSceneData);

	VkDescriptorBufferInfo camInfo = g_RenderState.GetCurrentFrame().DynamicData.Source.GetInfo();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.CompactedInstanceBuffer.GetInfo();

	if (!instanceInfo.buffer) return;

	VkDescriptorImageInfo shadowImage;
	shadowImage.sampler = g_RenderState.ShadowSampler;

	shadowImage.imageView = g_RenderState.ShadowImage.DefaultView;
	shadowImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorSet GlobalSet;

	vk::descriptor::builder::descriptor_bind binds1[] =
	{
		{ 0, &camInfo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT },
		{ 1, &sceneInfo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT },
		{ 2, nullptr, &shadowImage, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, GlobalSet, binds1, 3);

	VkDescriptorSet ObjectDataSet;

	vk::descriptor::builder::descriptor_bind binds2[] =
	{
		{ 0, &objectBufferInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
		{ 1, &instanceInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, ObjectDataSet, binds2, 2);

	vkCmdSetDepthBias(cmd, 0, 0, 0);

	std::vector<u32> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);
	dynamic_offsets.push_back(scene_data_offset);
	ExecuteDrawCommands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}

void
DrawObjectsShadow(VkCommandBuffer cmd,
									render_scene::mesh_pass& pass)
{
	// ZoneScopedNC("DrawObjects", tracy::Color::Blue);

	glm::mat4 view = g_RenderState.MainLight.get_view();

	glm::mat4 projection = g_RenderState.MainLight.get_projection();

	GPUCameraData camData;
	camData.Projection = projection;
	camData.View = view;
	camData.Viewproj = projection * view;

	//push data to dynmem
	uint32_t camera_data_offset = g_RenderState.GetCurrentFrame().DynamicData.Push(camData);

	VkDescriptorBufferInfo objectBufferInfo = g_RenderState.RenderScene->ObjectDataBuffer.GetInfo();

	VkDescriptorBufferInfo camInfo = g_RenderState.GetCurrentFrame().DynamicData.Source.GetInfo();
	camInfo.range = sizeof(GPUCameraData);

	VkDescriptorBufferInfo instanceInfo = pass.CompactedInstanceBuffer.GetInfo();

	VkDescriptorSet GlobalSet;

	vk::descriptor::builder::descriptor_bind binds1[] =
	{
		{ 0, &camInfo, nullptr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, GlobalSet, binds1, 1);

	VkDescriptorSet ObjectDataSet;

	vk::descriptor::builder::descriptor_bind binds2[] =
	{
		{ 0, &objectBufferInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT },
		{ 1, &instanceInfo, nullptr, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
	};

	vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, ObjectDataSet, binds2, 2);

	vkCmdSetDepthBias(cmd, SHADOW_BIAS, 0, SLOPE_BIAS);

	std::vector<u32> dynamic_offsets;
	dynamic_offsets.push_back(camera_data_offset);

	ExecuteDrawCommands(cmd, pass, ObjectDataSet, dynamic_offsets, GlobalSet);
}

struct alignas(16)
DepthReduceData
{
	glm::vec2 imageSize;
};

inline u32
GetGroupCount(u32 threadCount,
							u32 localSize)
{
	return (threadCount + localSize - 1) / localSize;
}

void
ReduceDepth(VkCommandBuffer cmd)
{
	// vkutil::VulkanScopeTimer timer(cmd, _profiler, "Depth Reduce");

	VkImageMemoryBarrier depthReadBarriers[] =
	{
		vk::init::image_barrier(g_RenderState.DepthImage.Image, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT),
	};

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, depthReadBarriers);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.DepthReducePipeline);

	for (i32 i = 0; i < g_RenderState.DepthPyramidLevels; ++i)
	{
		VkDescriptorImageInfo destTarget;
		destTarget.sampler = g_RenderState.DepthSampler;
		destTarget.imageView = g_RenderState.DepthPyramidMips[i];
		destTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkDescriptorImageInfo sourceTarget;
		sourceTarget.sampler = g_RenderState.DepthSampler;
		if (i == 0)
		{
			sourceTarget.imageView = g_RenderState.DepthImage.DefaultView;
			sourceTarget.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		else
		{
			sourceTarget.imageView = g_RenderState.DepthPyramidMips[i - 1];
			sourceTarget.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}

		VkDescriptorSet depthSet;

		vk::descriptor::builder::descriptor_bind binds1[] =
		{
			{ 0, nullptr, &destTarget, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT },
			{ 1, nullptr, &sourceTarget, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT }
		};

		vk::descriptor::builder::BuildDescriptorSet(*g_RenderState.GetCurrentFrame().DynamicDescriptorAllocator, *g_RenderState.DescriptorLayoutCache, depthSet, binds1, 2);

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_RenderState.DepthReduceLayout, 0, 1, &depthSet, 0, nullptr);

		u32 levelWidth = g_RenderState.DepthPyramidWidth >> i;
		u32 levelHeight = g_RenderState.DepthPyramidHeight >> i;

		if (levelHeight < 1)
			levelHeight = 1;
		if (levelWidth < 1)
			levelWidth = 1;

		DepthReduceData reduceData = {glm::vec2(levelWidth, levelHeight)};

		vkCmdPushConstants(cmd, g_RenderState.DepthReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(reduceData), &reduceData);
		vkCmdDispatch(cmd, GetGroupCount(levelWidth, 32), GetGroupCount(levelHeight, 32), 1);

		VkImageMemoryBarrier reduceBarrier = vk::init::image_barrier(g_RenderState.DepthPyramid.Image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);

		//VkImageMemoryBarrier reduceBarrier = vk::init::image_barrier(g_RenderState.DepthPyramid.Image, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
		//vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &reduceBarrier);

		// VkImageMemoryBarrier readBarrier = imageBarrier(depthPyramid.image, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		// vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 1, &fillBarrier, 1, &readBarrier);
	}

	VkImageMemoryBarrier depthWriteBarrier = vk::init::image_barrier(g_RenderState.DepthImage.Image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, &depthWriteBarrier);
}

}
