
namespace assets
{

  enum class
  compression_mode : u32
  {
    NONE = 0,
    LZ4
  };

  struct
  asset_file
  {
    char Type[4];
    u32 Version;
    std::string Json;
    std::vector<byte> BinaryBlob;
  };

  internal void
  SaveBinaryFile(const char* Path,
                 const asset_file& File)
  {
    FILE* writeFile = fopen(Path, "wb");

    fwrite(File.Type, sizeof(char), 4, writeFile);
    fwrite(&File.Version, sizeof(u32), 1, writeFile);
    u32 length = (u32)File.Json.size();
    fwrite(&length, sizeof(u32), 1, writeFile);
    u32 blobLength = (u32)File.BinaryBlob.size();
    fwrite(&blobLength, sizeof(u32), 1, writeFile);
    fwrite(File.Json.data(), sizeof(char), length, writeFile);
    fwrite(File.BinaryBlob.data(), sizeof(byte), blobLength, writeFile);

    fclose(writeFile);
  }

  internal void
  LoadBinaryFile(const char* Path,
                 asset_file& OutFile)
  {
    FILE* readFile = fopen(Path, "rb");

    fread(OutFile.Type, sizeof(char), 4, readFile);
    fread(&OutFile.Version, sizeof(u32), 1, readFile);
    u32 jsonLength;
    fread(&jsonLength, sizeof(u32), 1, readFile);
    u32 blobLength;
    fread(&blobLength, sizeof(u32), 1, readFile);
    OutFile.Json.resize(jsonLength);
    fread(OutFile.Json.data(), sizeof(char), jsonLength, readFile);
    OutFile.BinaryBlob.resize(blobLength);
    fread(OutFile.BinaryBlob.data(), sizeof(byte), blobLength, readFile);

    fclose(readFile);
  }
  
  internal compression_mode
  ParseCompression(const char* f)
  {
    if(strcmp(f, "LZ4") == 0)
    {
      return compression_mode::LZ4;
    }
    return compression_mode::NONE;
  }

}