// Extract a named asset from the .hosdict container to a file on disk.
// Usage: asset_extract <asset_name> <out_path> [dict_dir]

#include "hosdict_container.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace hoskey::engine;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: asset_extract <asset_name> <out_path> [dict_dir]\n");
    return 1;
  }
  const char* dir = argc > 3 ? argv[3]
      : "C:/Users/Kharki/DevEcoStudioProjects/HOSKEY/entry/src/main/resources/rawfile/dict";
  std::string container;
  for (auto& e : std::filesystem::directory_iterator(dir))
    if (e.path().extension() == ".hosdict") { container = e.path().string(); break; }
  HosdictContainer c;
  if (!c.Open(container).ok()) { std::printf("open FAILED\n"); return 1; }
  const uint8_t* data = nullptr; size_t data_size = 0;
  if (!c.GetAssetData(argv[1], &data, &data_size).ok()) {
    std::printf("asset '%s' not found\n", argv[1]);
    return 1;
  }
  FILE* f = std::fopen(argv[2], "wb");
  if (!f) { std::printf("cannot open out\n"); return 1; }
  std::fwrite(data, 1, data_size, f);
  std::fclose(f);
  std::printf("wrote %zu bytes to %s\n", data_size, argv[2]);
  return 0;
}
