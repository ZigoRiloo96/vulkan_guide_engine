

#define U16Max 65535
#define I32Min ((s32)0x80000000)
#define I32Max ((s32)0x7fffffff)
#define U32Max ((u32)-1)
#define U64Max ((u64)-1)
#define F32Max FLT_MAX
#define F32Min -FLT_MAX

#if !defined(internal)
#define internal static
#endif
#define local_persist static
#define global_variable static

#if VKE_SLOW
#define Assert(Expression) if(!(Expression)) { logger::log("[ASSERT][FILE: %s][LINE: %d]",__FILE__, __LINE__); logger::term(); *(int *)0 = 0;}
#else
#define Assert(Expression)
#endif

#define InvalidCodePath Assert(!"InvalidCodePath")
#define InvalidDefaultCase default: {InvalidCodePath;} break

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

inline f32
AbsoluteValue(f32 Real32)
{
    f32 Result = fabsf(Real32);
    return(Result);
}

struct platform_memory_block
{
  u64 Flags;
  u64 Size;
  u8* Base;
  uptr Used;
  platform_memory_block* Prev;
};

typedef struct textured_vertex
{
  glm::vec4 P;
  glm::vec2 UV;
  u32 Color;

  glm::vec3 N;
} textured_vertex;

typedef struct game_render_settings
{
  u32 Width;
  u32 Height;
  u32 DepthPeelCountHint;
  b32 MultisamplingDebug;
  b32 MultisamplingHint;
  b32 PixelationHint;
  b32 LightingDisabled;
} game_render_settings;

typedef struct game_render_commands
{
  game_render_settings Settings;

  u32 MaxPushBufferSize;
  u8* PushBufferBase;
  u8* PushBufferDataAt;

  u32 MaxVertexCount;
  u32 VertexCount;
  textured_vertex* VertexArray;
} game_render_commands;