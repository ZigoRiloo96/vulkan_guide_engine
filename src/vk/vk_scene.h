

struct MeshObject
{
	Mesh* Mesh { nullptr };

	vk::util::material* Material;
	u32 CustomSortKey;
	glm::mat4 TransformMatrix;

	RenderBounds Bounds;

	u32 bDrawForwardPass : 1;
	u32 bDrawShadowPass : 1;
};

template<typename T>
struct
handle
{
  u32 Handle;
};

struct
gpu_indirect_object
{
  VkDrawIndexedIndirectCommand Command;
	u32 ObjectID;
	u32 BatchID;
};

struct
draw_mesh
{
  u32 FirstVertex;
  u32 FirstIndex;
  u32 IndexCount;
  u32 VertexCount;
  bool IsMerged;

  Mesh* Original;
};

struct
render_object
{
  handle<draw_mesh> MeshID;
  handle<vk::util::material> Material;

  u32 UpdateIndex;
  u32 CustomSortKey {0};

  vk::util::per_pass_data<i32> PassIndices;

  glm::mat4 TransformMatrix;

  RenderBounds Bounds;
};

struct
gpu_instance
{
  u32 ObjectID;
  u32 BatchID;
};

struct
render_scene
{
  struct
  pass_material
  {
    VkDescriptorSet MaterialSet;
		vk::util::shader_pass* ShaderPass;

		bool
    operator==(const pass_material& other) const
		{
			return MaterialSet == other.MaterialSet && ShaderPass == other.ShaderPass;
		}
  };

  struct
  pass_object
  {
		pass_material Material;
		handle<draw_mesh> MeshID;
		handle<render_object> Original;
		i32 Builtbatch;
		u32 CustomKey;
	};
  
  struct
  render_batch
  {
		handle<pass_object> Object;
		u64 SortKey;

		bool
    operator==(const render_batch& other) const
		{
			return Object.Handle == other.Object.Handle && SortKey == other.SortKey;
		}
	};

  struct
  indirect_batch
  {
		handle<draw_mesh> MeshID;
		pass_material Material;
		u32 First;
		u32 Count;
	};

  struct
  multibatch
  {
		u32 First;
		u32 Count;
	};

  struct
  mesh_pass
  {
		std::vector<multibatch> Multibatches;

		std::vector<indirect_batch> Batches;

		std::vector<handle<render_object>> UnbatchedObjects;

		std::vector<render_batch> FlatBatches;

		std::vector<pass_object> Objects;

		std::vector<handle<pass_object>> ReusableObjects;

		std::vector<handle<pass_object>> ObjectsToDelete;

		
		allocated_buffer<u32> CompactedInstanceBuffer;
		allocated_buffer<gpu_instance> PassObjectsBuffer;

		allocated_buffer<gpu_indirect_object> DrawIndirectBuffer;
		allocated_buffer<gpu_indirect_object> ClearIndirectBuffer;

		pass_object* Get(handle<pass_object> handle)
    {
      return &Objects[handle.Handle];
    }

		MeshpassType Type;

		bool NeedsIndirectRefresh = true;
		bool NeedsInstanceRefresh = true;
	};

  void
  Init()
  {
    ForwardPass.Type = MeshpassType::Forward;
	  ShadowPass.Type = MeshpassType::DirectionalShadow;
	  TransparentForwardPass.Type = MeshpassType::Transparency;
  }

  handle<render_object>
  RegisterObject(MeshObject* object)
  {
    render_object newObj;
    newObj.Bounds = object->Bounds;
    newObj.TransformMatrix = object->TransformMatrix;
    newObj.Material = GetMaterialHandle(object->Material);
    newObj.MeshID = GetMeshHandle(object->Mesh);
    newObj.UpdateIndex = (u32)-1;
    newObj.CustomSortKey = object->CustomSortKey;
    newObj.PassIndices.Clear(-1);
    handle<render_object> handle;
    handle.Handle = static_cast<u32>(Renderables.size());

    Renderables.push_back(newObj);

    if (object->bDrawForwardPass)
    {
      if (object->Material->Original->PassShaders[MeshpassType::Transparency])
      {
        TransparentForwardPass.UnbatchedObjects.push_back(handle);
      }
      if (object->Material->Original->PassShaders[MeshpassType::Forward])
      {
        ForwardPass.UnbatchedObjects.push_back(handle);
      }
    }
    if (object->bDrawShadowPass)
    {
      if (object->Material->Original->PassShaders[MeshpassType::DirectionalShadow])
      {
        ShadowPass.UnbatchedObjects.push_back(handle);
      }
    }

    UpdateObject(handle);

    return handle;
  }

  void
  RegisterObjectBatch(MeshObject* first, u32 count)
  {
    Renderables.reserve(count);

	  for (u32 i = 0; i < count; i++)
    {
		  RegisterObject(&(first[i]));
	  }
  }

  void
  UpdateTransform(handle<render_object> objectID, const glm::mat4 &localToWorld)
  {
    GetObject(objectID)->TransformMatrix = localToWorld;
	  UpdateObject(objectID);
  }

  void
  UpdateObject(handle<render_object> objectID)
  {
    auto &passIndices = GetObject(objectID)->PassIndices;
    if (passIndices[MeshpassType::Forward] != -1)
    {
      handle<pass_object> obj;
      obj.Handle = passIndices[MeshpassType::Forward];

      ForwardPass.ObjectsToDelete.push_back(obj);
      ForwardPass.UnbatchedObjects.push_back(objectID);

      passIndices[MeshpassType::Forward] = -1;
    }

    if (passIndices[MeshpassType::DirectionalShadow] != -1)
    {
      handle<pass_object> obj;
      obj.Handle = passIndices[MeshpassType::DirectionalShadow];

      ShadowPass.ObjectsToDelete.push_back(obj);
      ShadowPass.UnbatchedObjects.push_back(objectID);

      passIndices[MeshpassType::DirectionalShadow] = -1;
    }

    if (passIndices[MeshpassType::Transparency] != -1)
    {
      handle<pass_object> obj;
      obj.Handle = passIndices[MeshpassType::Transparency];

      TransparentForwardPass.UnbatchedObjects.push_back(objectID);
      TransparentForwardPass.ObjectsToDelete.push_back(obj);

      passIndices[MeshpassType::Transparency] = -1;
    }

    if (GetObject(objectID)->UpdateIndex == (u32)-1)
    {

      GetObject(objectID)->UpdateIndex = static_cast<u32>(DirtyObjects.size());

      DirtyObjects.push_back(objectID);
    }
  }

  void
  FillObjectData(vk::renderer::GPUObjectData* data)
  {
    for(int i = 0; i < Renderables.size(); i++)
	  {
		  handle<render_object> h;
		  h.Handle = i;
		  WriteObject(data + i, h);
	  }
  }

  void
  FillIndirectArray(gpu_indirect_object* data, mesh_pass& pass)
  {
    //ZoneScopedNC("Fill Indirect", tracy::Color::Red);
    int dataIndex = 0;
    for (int i = 0; i < pass.Batches.size(); i++)
    {
      auto batch = pass.Batches[i];

      data[dataIndex].Command.firstInstance = batch.First; //i;
      data[dataIndex].Command.instanceCount = 0;
      data[dataIndex].Command.firstIndex = GetMesh(batch.MeshID)->FirstIndex;
      data[dataIndex].Command.vertexOffset = GetMesh(batch.MeshID)->FirstVertex;
      data[dataIndex].Command.indexCount = GetMesh(batch.MeshID)->IndexCount;
      data[dataIndex].ObjectID = 0;
      data[dataIndex].BatchID = i;

      dataIndex++;
    }
  }

  void
  FillInstancesArray(gpu_instance* data, mesh_pass& pass)
  {
    //ZoneScopedNC("Fill Instances", tracy::Color::Red);
    int dataIndex = 0;
    for (u32 i = 0; i < pass.Batches.size(); i++)
    {
      auto batch = pass.Batches[i];

      for (u32 b = 0; b < batch.Count; b++)
      {
        data[dataIndex].ObjectID = pass.Get(pass.FlatBatches[b + batch.First].Object)->Original.Handle;
        data[dataIndex].BatchID = i;
        dataIndex++;
      }
    }
  }

  void
  WriteObject(vk::renderer::GPUObjectData* target, handle<render_object> objectID)
  {
    render_object* renderable = GetObject(objectID);
	  vk::renderer::GPUObjectData object;

	  object.ModelMatrix = renderable->TransformMatrix;
	  object.OriginRad = glm::vec4(renderable->Bounds.Origin, renderable->Bounds.Radius);
	  object.Extents = glm::vec4(renderable->Bounds.Extents, renderable->Bounds.Valid ? 1.f : 0.f);

	  memcpy(target, &object, sizeof(vk::renderer::GPUObjectData));
  }

  void
  ClearDirtyObjects()
  {
    for (auto obj : DirtyObjects)
    {
      GetObject(obj)->UpdateIndex = (u32)-1;
    }
    DirtyObjects.clear();
  }

	void
  BuildBatches()
  { // mt
    RefreshPass(&ForwardPass);
	  RefreshPass(&TransparentForwardPass);
	  RefreshPass(&ShadowPass);
  }

	void
  MergeMeshes(/*class VulkanEngine* engine*/)
  {
    //ZoneScopedNC("Mesh Merge", tracy::Color::Magenta)
    u64 total_vertices = 0;
    u64 total_indices = 0;

    for (auto &m : Meshes)
    {
      m.FirstIndex = static_cast<u32>(total_indices);
      m.FirstVertex = static_cast<u32>(total_vertices);

      total_vertices += m.VertexCount;
      total_indices += m.IndexCount;

      m.IsMerged = true;
    }

    MergedVertexBuffer = vk::renderer::CreateBuffer(total_vertices * sizeof(Vertex), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_GPU_ONLY);

    MergedIndexBuffer = vk::renderer::CreateBuffer(total_indices * sizeof(u32), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                              VMA_MEMORY_USAGE_GPU_ONLY);

    vk::renderer::ImmediateSubmit([&](VkCommandBuffer cmd)
    {
      for (auto &m : Meshes)
      {
        VkBufferCopy vertexCopy;
        vertexCopy.dstOffset = m.FirstVertex * sizeof(Vertex);
        vertexCopy.size = m.VertexCount * sizeof(Vertex);
        vertexCopy.srcOffset = 0;
        vkCmdCopyBuffer(cmd, m.Original->VertexBuffer.Buffer, MergedVertexBuffer.Buffer, 1, &vertexCopy);

        if (m.IndexCount > 0)
        {
          VkBufferCopy indexCopy;
          indexCopy.dstOffset = m.FirstIndex * sizeof(u32);
          indexCopy.size = m.IndexCount * sizeof(u32);
          indexCopy.srcOffset = 0;
          vkCmdCopyBuffer(cmd, m.Original->IndexBuffer.Buffer, MergedIndexBuffer.Buffer, 1, &indexCopy);
        }
      }
    });
  }

	void
  RefreshPass(mesh_pass* pass)
  {
    pass->NeedsIndirectRefresh = true;
    pass->NeedsInstanceRefresh = true;

    std::vector<u32> new_objects;

    if (pass->ObjectsToDelete.size() > 0)
    {
      //ZoneScopedNC("Delete objects", tracy::Color::Blue3);

      //create the render batches so that then we can do the deletion on the flat-array directly

      std::vector<render_batch> deletion_batches;
      deletion_batches.reserve(new_objects.size());

      for (auto i : pass->ObjectsToDelete)
      {
        pass->ReusableObjects.push_back(i);
        render_batch newCommand;

        auto obj = pass->Objects[i.Handle];
        newCommand.Object = i;

        u64 pipelinehash = std::hash<u64>()(u64(obj.Material.ShaderPass->Pipeline));
        u64 sethash = std::hash<u64>()((u64)obj.Material.MaterialSet);

        u32 mathash = static_cast<u32>(pipelinehash ^ sethash);

        u32 meshmat = u64(mathash) ^ u64(obj.MeshID.Handle);

        //pack mesh id and material into 64 bits
        newCommand.SortKey = u64(meshmat) | (u64(obj.CustomKey) << 32);

        pass->Objects[i.Handle].CustomKey = 0;
        pass->Objects[i.Handle].Material.ShaderPass = nullptr;
        pass->Objects[i.Handle].MeshID.Handle = (u32)-1;
        pass->Objects[i.Handle].Original.Handle = (u32)-1;

        deletion_batches.push_back(newCommand);
      }
      pass->ObjectsToDelete.clear();
      {
        // ZoneScopedNC("Deletion Sort", tracy::Color::Blue1);
        std::sort(deletion_batches.begin(), deletion_batches.end(), [](const render_batch &A, const render_batch &B)
        {
          if (A.SortKey < B.SortKey)
          {
            return true;
          }
          else if (A.SortKey == B.SortKey)
          {
            return A.Object.Handle < B.Object.Handle;
          }
          else
          {
            return false;
          }
        });
      }
      {
        // ZoneScopedNC("removal", tracy::Color::Blue1);

        std::vector<render_batch> newbatches;
        newbatches.reserve(pass->FlatBatches.size());

        {
          // ZoneScopedNC("Set Difference", tracy::Color::Red);

          std::set_difference(pass->FlatBatches.begin(),
                              pass->FlatBatches.end(),
                              deletion_batches.begin(),
                              deletion_batches.end(),
                              std::back_inserter(newbatches),
          [](const render_batch &A, const render_batch &B)
          {
            if (A.SortKey < B.SortKey)
            {
              return true;
            }
            else if (A.SortKey == B.SortKey)
            {
              return A.Object.Handle < B.Object.Handle;
            }
            else
            {
              return false;
            }
          });
        }
        pass->FlatBatches = std::move(newbatches);
      }
    }
    {
      //ZoneScopedNC("Fill ObjectList", tracy::Color::Blue2);

      new_objects.reserve(pass->UnbatchedObjects.size());
      for (auto o : pass->UnbatchedObjects)
      {
        pass_object newObject;

        newObject.Original = o;
        newObject.MeshID = GetObject(o)->MeshID;

        //pack mesh id and material into 32 bits
        vk::util::material *mt = GetMaterial(GetObject(o)->Material);
        newObject.Material.MaterialSet = mt->PassSets[pass->Type];
        newObject.Material.ShaderPass = mt->Original->PassShaders[pass->Type];
        newObject.CustomKey = GetObject(o)->CustomSortKey;

        u32 handle = (u32)-1;

        //reuse handle
        if (pass->ReusableObjects.size() > 0)
        {
          handle = pass->ReusableObjects.back().Handle;
          pass->ReusableObjects.pop_back();
          pass->Objects[handle] = newObject;
        }
        else
        {
          handle = (u32)pass->Objects.size();
          pass->Objects.push_back(newObject);
        }

        new_objects.push_back(handle);
        GetObject(o)->PassIndices[pass->Type] = static_cast<i32>(handle);
      }

      pass->UnbatchedObjects.clear();
    }

    std::vector<render_batch> new_batches;
    new_batches.reserve(new_objects.size());

    {
      // ZoneScopedNC("Fill DrawList", tracy::Color::Blue2);

      for (auto i : new_objects)
      {
        {
          render_batch newCommand;

          auto obj = pass->Objects[i];
          newCommand.Object.Handle = i;

          u64 pipelinehash = std::hash<u64>()((u64)obj.Material.ShaderPass->Pipeline);
          u64 sethash = std::hash<u64>()((u64)obj.Material.MaterialSet);

          u32 mathash = static_cast<u32>(pipelinehash ^ sethash);

          u32 meshmat = u64(mathash) ^ u64(obj.MeshID.Handle);

          //pack mesh id and material into 64 bits
          newCommand.SortKey = u64(meshmat) | (u64(obj.CustomKey) << 32);

          new_batches.push_back(newCommand);
        }
      }
    }

    {
      // ZoneScopedNC("Draw Sort", tracy::Color::Blue1);
      std::sort(new_batches.begin(), new_batches.end(),
      [](const render_batch &A, const render_batch &B)
      {
        if (A.SortKey < B.SortKey)
        {
          return true;
        }
        else if (A.SortKey == B.SortKey)
        {
          return A.Object.Handle < B.Object.Handle;
        }
        else
        {
          return false;
        }
      });
    }
    {
      // ZoneScopedNC("Draw Merge batches", tracy::Color::Blue2);

      //merge the new batches into the main batch array

      if (pass->FlatBatches.size() > 0 && new_batches.size() > 0)
      {
        u64 index = pass->FlatBatches.size();
        pass->FlatBatches.reserve(pass->FlatBatches.size() + new_batches.size());

        for (auto b : new_batches)
        {
          pass->FlatBatches.push_back(b);
        }

        render_batch *begin = pass->FlatBatches.data();
        render_batch *mid = begin + index;
        render_batch *end = begin + pass->FlatBatches.size();
        //std::sort(pass->flat_batches.begin(), pass->flat_batches.end(), [](const RenderScene::RenderBatch& A, const RenderScene::RenderBatch& B) {
        //	return A.sortKey < B.sortKey;
        //	});
        std::inplace_merge(begin, mid, end,
        [](const render_batch &A, const render_batch &B)
        {
          if (A.SortKey < B.SortKey)
          {
            return true;
          }
          else if (A.SortKey == B.SortKey)
          {
            return A.Object.Handle < B.Object.Handle;
          }
          else
          {
            return false;
          }
        });
      }
      else if (pass->FlatBatches.size() == 0)
      {
        pass->FlatBatches = std::move(new_batches);
      }
    }

    {
      // ZoneScopedNC("Draw Merge", tracy::Color::Blue);

      pass->Batches.clear();

      BuildIndirectBatches(pass, pass->Batches, pass->FlatBatches);

      //flatten batches into multibatch
      multibatch newbatch;
      pass->Multibatches.clear();

      newbatch.Count = 1;
      newbatch.First = 0;

#if 1
      for (int i = 1; i < pass->Batches.size(); i++)
      {
        indirect_batch *joinbatch = &pass->Batches[newbatch.First];
        indirect_batch *batch = &pass->Batches[i];

        bool bCompatibleMesh = GetMesh(joinbatch->MeshID)->IsMerged;

        bool bSameMat = false;

        if (bCompatibleMesh && joinbatch->Material.MaterialSet == batch->Material.MaterialSet &&
            joinbatch->Material.ShaderPass == batch->Material.ShaderPass)
        {
          bSameMat = true;
        }

        if (!bSameMat || !bCompatibleMesh)
        {
          pass->Multibatches.push_back(newbatch);
          newbatch.Count = 1;
          newbatch.First = i;
        }
        else
        {
          newbatch.Count++;
        }
      }
      pass->Multibatches.push_back(newbatch);
#else
      for (int i = 0; i < pass->batches.size(); i++)
      {
        Multibatch newbatch;
        newbatch.count = 1;
        newbatch.first = i;

        pass->multibatches.push_back(newbatch);
      }
#endif
    }
  }

  void
  BuildIndirectBatches(mesh_pass* pass,
                       std::vector<indirect_batch>& outbatches,
                       std::vector<render_batch>& inobjects)
  {
    if (inobjects.size() == 0)
      return;

    // ZoneScopedNC("Build Indirect Batches", tracy::Color::Blue);

    indirect_batch newBatch;
    newBatch.First = 0;
    newBatch.Count = 0;

    newBatch.Material = pass->Get(inobjects[0].Object)->Material;
    newBatch.MeshID = pass->Get(inobjects[0].Object)->MeshID;

    outbatches.push_back(newBatch);
    indirect_batch *back = &pass->Batches.back();

    pass_material lastMat = pass->Get(inobjects[0].Object)->Material;
    for (int i = 0; i < inobjects.size(); i++)
    {
      pass_object *obj = pass->Get(inobjects[i].Object);

      bool bSameMesh = obj->MeshID.Handle == back->MeshID.Handle;
      bool bSameMaterial = false;
      if (obj->Material == lastMat)
      {
        bSameMaterial = true;
      }

      if (!bSameMaterial || !bSameMesh)
      {
        newBatch.Material = obj->Material;

        if (newBatch.Material == back->Material)
        {
          bSameMaterial = true;
        }
      }

      if (bSameMesh && bSameMaterial)
      {
        back->Count++;
      }
      else
      {
        newBatch.First = i;
        newBatch.Count = 1;
        newBatch.MeshID = obj->MeshID;

        outbatches.push_back(newBatch);
        back = &outbatches.back();
      }
      //back->objects.push_back(obj->original);
    }
  }

	render_object*
  GetObject(handle<render_object> objectID)
  {
    return &Renderables[objectID.Handle];
  }

	draw_mesh*
  GetMesh(handle<draw_mesh> objectID)
  {
    return &Meshes[objectID.Handle];
  }

	vk::util::material*
  GetMaterial(handle<vk::util::material> objectID)
  {
    return Materials[objectID.Handle];
  }

	std::vector<render_object> Renderables;
	std::vector<draw_mesh> Meshes;
	std::vector<vk::util::material*> Materials;

	std::vector<handle<render_object>> DirtyObjects;

	mesh_pass*
  GetMeshPass(MeshpassType name)
  {
    switch (name)
    {
    case MeshpassType::Forward:
      return &ForwardPass;
      break;
    case MeshpassType::Transparency:
      return &TransparentForwardPass;
      break;
    case MeshpassType::DirectionalShadow:
      return &ShadowPass;
      break;
    }
    return nullptr;
  }

	mesh_pass ForwardPass;
	mesh_pass TransparentForwardPass;
	mesh_pass ShadowPass;

	std::unordered_map<vk::util::material*, handle<vk::util::material>> MaterialConvert;
	std::unordered_map<Mesh*, handle<draw_mesh>> MeshConvert;

	handle<vk::util::material> GetMaterialHandle(vk::util::material* m)
  {
    handle<vk::util::material> handle;
    auto it = MaterialConvert.find(m);
    if (it == MaterialConvert.end())
    {
      u32 index = static_cast<u32>(Materials.size());
      Materials.push_back(m);

      handle.Handle = index;
      MaterialConvert[m] = handle;
    }
    else
    {
      handle = (*it).second;
    }
    return handle;
  }

	handle<draw_mesh> GetMeshHandle(Mesh* m)
  {
    handle<draw_mesh> handle;
    auto it = MeshConvert.find(m);
    if (it == MeshConvert.end())
    {
      u32 index = static_cast<u32>(Meshes.size());

      draw_mesh newMesh;
      newMesh.Original = m;
      newMesh.FirstIndex = 0;
      newMesh.FirstVertex = 0;
      newMesh.VertexCount = static_cast<u32>(m->Vertices.size());
      newMesh.IndexCount = static_cast<u32>(m->Indices.size());

      Meshes.push_back(newMesh);

      handle.Handle = index;
      MeshConvert[m] = handle;
    }
    else
    {
      handle = (*it).second;
    }
    return handle;
  }
	

	allocated_buffer<Vertex> MergedVertexBuffer;
	allocated_buffer<u32> MergedIndexBuffer;

	allocated_buffer<vk::renderer::GPUSceneData> ObjectDataBuffer;

};




