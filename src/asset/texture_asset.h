

namespace assets
{

  enum class
  texture_format : u32
  {
    NONE = 0,
    RGBA8
  };

  struct
  page_info
  {
    u32 Width;
    u32 Height;
    u32 CompressedSize;
    u32 OriginalSize;
  };

  struct
  texture_info
  {
    u64 Size;
    texture_format Format;
    compression_mode CompressionMode;

    std::string OriginalFile;
    std::vector<page_info> Pages;
    //u32 Pixelsize[3];
  };

  internal texture_format
  ParseFormat(const char* f)
  {
    if (strcmp(f, "RGBA8") == 0)
    {
      return texture_format::RGBA8;
    }
    else
    {
      return texture_format::NONE;
    }
  }

  internal texture_info
  ReadTextureInfo(asset_file* File)
  {
    texture_info info;

    nlohmann::json texture_metadata = nlohmann::json::parse(File->Json);

    std::string formatString = texture_metadata["format"];
    info.Format = ParseFormat(formatString.c_str());

    std::string compressionString = texture_metadata["compression"];
    info.CompressionMode = ParseCompression(compressionString.c_str());

    info.Size = texture_metadata["buffer_size"];
    info.OriginalFile = texture_metadata["original_file"];

    for (auto &[key, value] : texture_metadata["pages"].items())
    {
      page_info page;

      page.CompressedSize = value["compressed_size"];
      page.OriginalSize = value["original_size"];
      page.Width = value["width"];
      page.Height = value["height"];

      info.Pages.push_back(page);
    }

    return info;
  }

  internal void
  UnpackTexture(texture_info* Info,
                const char* SourceBuffer,
                u64 SourceSize,
                char* Destination)
  {
    if (Info->CompressionMode == compression_mode::LZ4)
    {
      for (auto &page : Info->Pages)
      {
        LZ4_decompress_safe(SourceBuffer, Destination, page.CompressedSize, page.OriginalSize);
        SourceBuffer += page.CompressedSize;
        Destination += page.OriginalSize;
      }
    }
    else
    {
      memcpy(Destination, SourceBuffer, SourceSize);
    }
  }

  internal void
  UnpackTexturePage(texture_info* Info,
                    i32 PageIndex,
                    char* SourceBuffer,
                    char* Destination)
  {
    char *source = SourceBuffer;

    for (int i = 0; i < PageIndex; i++)
    {
      source += Info->Pages[i].CompressedSize;
    }

    if (Info->CompressionMode == compression_mode::LZ4)
    {
      if (Info->Pages[PageIndex].CompressedSize != Info->Pages[PageIndex].OriginalSize)
      {
        LZ4_decompress_safe(source, Destination, Info->Pages[PageIndex].CompressedSize, Info->Pages[PageIndex].OriginalSize);
        return;
      }
    }

    memcpy(Destination, source, Info->Pages[PageIndex].OriginalSize);
  }

  internal asset_file
  PackTexture(texture_info* Info,
              void* PixelData)
  {
    asset_file file;
    file.Type[0] ='T';file.Type[1] ='E';file.Type[2] ='X';file.Type[3] ='I';
    file.Version = 1;

    char* pixels = (char*)PixelData;
    std::vector<char> page_buffer;

    for (auto& p : Info->Pages)
    {
      page_buffer.resize(p.OriginalSize);

      i32 compressStaging = LZ4_compressBound(p.OriginalSize);
      page_buffer.resize(compressStaging);

      i32 compressedSize = LZ4_compress_default(pixels, page_buffer.data(), p.OriginalSize, compressStaging);

      f32 compressionRate = f32(compressedSize) / f32(Info->Size);

      if(compressionRate > 0.8)
      {
        compressedSize = p.OriginalSize;
        page_buffer.resize(compressedSize);
        memcpy(page_buffer.data(), pixels, compressedSize);
      }
      else
      {
        page_buffer.resize(compressedSize);
      }
      p.CompressedSize = compressedSize;

      file.BinaryBlob.insert(file.BinaryBlob.end(), page_buffer.begin(), page_buffer.end());

      pixels += p.OriginalSize;
    }
    
    nlohmann::json texture_metadata;
    texture_metadata["format"] = "RGBA8";
    
    texture_metadata["buffer_size"] = Info->Size;
    texture_metadata["original_file"] = Info->OriginalFile;
    texture_metadata["compression"] = "LZ4";

    std::vector<nlohmann::json> page_json;
    for (auto &p : Info->Pages)
    {
      nlohmann::json page;
      page["compressed_size"] = p.CompressedSize;
      page["original_size"] = p.OriginalSize;
      page["width"] = p.Width;
      page["height"] = p.Height;
      page_json.push_back(page);
    }
    texture_metadata["pages"] = page_json;

    std::string stringField = texture_metadata.dump();
    file.Json = stringField;

    return file;
  }

}