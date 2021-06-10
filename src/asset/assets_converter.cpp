
#include "../shared.h"

#include <filesystem>
namespace fs = std::filesystem;

#include <chrono>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset.h"
#include "texture_asset.h"
#include "prefab_asset.h"

struct
converter_state
{
  fs::path AssetPath;
  fs::path ExportPath;

  fs::path ConvertToExportRelative(fs::path path) const
  {
    return path.lexically_proximate(ExportPath);
  }
};

internal void
ConvertImage(const fs::path& Input, const fs::path& Output)
{
  i32 texWidth, texHeight, texChannels;
  auto pngstart = std::chrono::high_resolution_clock::now();
  stbi_uc* pixels = stbi_load(Input.u8string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  auto pngend = std::chrono::high_resolution_clock::now();
  auto diff = pngend - pngstart;

  logger::log("png took %f ms", std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0);

  if (!pixels)
  {
    Assert(false);
  }

  i32 texture_size = texWidth * texHeight * 4;

  assets::texture_info texInfo;
  texInfo.Size = texture_size;
  texInfo.Format = assets::texture_format::RGBA8;
  texInfo.OriginalFile = Input.string();
  auto start = std::chrono::high_resolution_clock::now();

  std::vector<char> all_buffer;

  texInfo.Pages.push_back({});
  texInfo.Pages.back().Width = texWidth;
  texInfo.Pages.back().Height = texHeight;
  texInfo.Pages.back().OriginalSize = (u32)texture_size;

  all_buffer.insert(all_buffer.end(), pixels, pixels + texture_size);
  
  all_buffer.resize(texture_size);
  memcpy(all_buffer.data(), pixels, texture_size);

  texInfo.Size = all_buffer.size();
  assets::asset_file newImage = assets::PackTexture(&texInfo, all_buffer.data());

  auto end = std::chrono::high_resolution_clock::now();

  diff = end - start;

  logger::log("compression took %f ms", std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count() / 1000000.0);

  stbi_image_free(pixels);

  assets::SaveBinaryFile(Output.string().c_str(), newImage);
}

int main(int argC, char* argV[])
{
  logger::init("conv_log.txt");

  if (argC < 2)
  {
    logger::err("You need to put the path to the info file");
    return -1;
  }

  fs::path path { argV[1] };
  fs::path directory = path;
  fs::path exported_dir = path.parent_path() / "assets_export";

  logger::log("loaded asset directory at %s", directory.u8string().c_str());

  converter_state convstate;
  convstate.AssetPath = path;
  convstate.ExportPath = exported_dir;

  for (auto &p : fs::recursive_directory_iterator(directory))
  {
    logger::log("File: %s", p.path().u8string().c_str());

    auto relative = p.path().lexically_proximate(directory);

    auto export_path = exported_dir / relative;

    if (!fs::is_directory(export_path.parent_path()))
    {
      fs::create_directory(export_path.parent_path());
    }

    if (p.path().extension() == ".png" || p.path().extension() == ".jpg" || p.path().extension() == ".TGA")
    {
      auto newpath = p.path();

      export_path.replace_extension(".tx");

      ConvertImage(p.path(), export_path);
    }
  }

  logger::term();

  return 0;
}