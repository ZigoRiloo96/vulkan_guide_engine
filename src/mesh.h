
struct
Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec3 Color;
    glm::vec2 UV;

    static VertexInputDescription
    get_vertex_description()
    {
      VertexInputDescription description;

      //we will have just 1 vertex buffer binding, with a per-vertex rate
      VkVertexInputBindingDescription mainBinding = {};
      mainBinding.binding = 0;
      mainBinding.stride = sizeof(Vertex);
      mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

      description.Bindings.push_back(mainBinding);

      //Position will be stored at Location 0
      VkVertexInputAttributeDescription positionAttribute = {};
      positionAttribute.binding = 0;
      positionAttribute.location = 0;
      positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
      positionAttribute.offset = offsetof(Vertex, Position);

      //Normal will be stored at Location 1
      VkVertexInputAttributeDescription normalAttribute = {};
      normalAttribute.binding = 0;
      normalAttribute.location = 1;
      normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
      normalAttribute.offset = offsetof(Vertex, Normal);

      //Color will be stored at Location 2
      VkVertexInputAttributeDescription colorAttribute = {};
      colorAttribute.binding = 0;
      colorAttribute.location = 2;
      colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
      colorAttribute.offset = offsetof(Vertex, Color);

      //UV will be stored at Location 3
      VkVertexInputAttributeDescription uvAttribute = {};
      uvAttribute.binding = 0;
      uvAttribute.location = 3;
      uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
      uvAttribute.offset = offsetof(Vertex, UV);

      description.Attributes.push_back(positionAttribute);
      description.Attributes.push_back(normalAttribute);
      description.Attributes.push_back(colorAttribute);
      description.Attributes.push_back(uvAttribute);
      
      return description;
    }
};

struct
Mesh
{
  std::vector<Vertex> Vertices;
  allocated_buffer VertexBuffer;

  void
  LoadFromObj(const char* filename)
  {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, nullptr);


    if (!warn.empty())
    {
      logger::warn("[TINYOBJ] %s", warn.c_str());
    }

    if (!err.empty())
    {
      logger::err("[TINYOBJ] %s", err.c_str());
      Assert(false);
    }

    for (size_t s = 0; s < shapes.size(); s++)
    {
      size_t index_offset = 0;
      for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
      {
        int fv = 3;
        for (size_t v = 0; v < fv; v++)
        {
          tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

          tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
          tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
          tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
          tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
          tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
          tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
        
          Vertex new_vert;
          new_vert.Position.x = vx;
          new_vert.Position.y = vy;
          new_vert.Position.z = vz;

          new_vert.Normal.x = nx;
          new_vert.Normal.y = ny;
          new_vert.Normal.z = nz;

          new_vert.Color = new_vert.Normal;

          tinyobj::real_t ux = attrib.texcoords[2 * idx.texcoord_index + 0];
          tinyobj::real_t uy = attrib.texcoords[2 * idx.texcoord_index + 1];
    
          new_vert.UV.x = ux;
          new_vert.UV.y = 1 - uy;

          Vertices.push_back(new_vert);
        }

        index_offset += fv;
      }
    }
  }
};

struct MeshPushConstants
{
  glm::vec4 Data;
  glm::mat4 RenderMatrix;
};
