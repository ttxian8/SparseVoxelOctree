#include "VoxLoader.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// MagicaVoxel默认调色板（简化版）
static const glm::u8vec4 DEFAULT_PALETTE[16] = {
    glm::u8vec4(0, 0, 0, 0),         // 透明
    glm::u8vec4(255, 255, 255, 255), // 白色
    glm::u8vec4(255, 255, 204, 255), // 浅黄
    glm::u8vec4(255, 255, 153, 255), // 黄色
    glm::u8vec4(255, 255, 102, 255), // 深黄
    glm::u8vec4(255, 255, 51, 255),  // 橙黄
    glm::u8vec4(255, 255, 0, 255),   // 纯黄
    glm::u8vec4(255, 204, 0, 255),   // 橙色
    glm::u8vec4(255, 153, 0, 255),   // 深橙
    glm::u8vec4(255, 102, 0, 255),   // 红橙
    glm::u8vec4(255, 51, 0, 255),    // 朱红
    glm::u8vec4(255, 0, 0, 255),     // 纯红
    glm::u8vec4(204, 0, 0, 255),     // 深红
    glm::u8vec4(153, 0, 0, 255),     // 暗红
    glm::u8vec4(102, 0, 0, 255),     // 棕红
    glm::u8vec4(51, 0, 0, 255)       // 暗棕
};

std::shared_ptr<VoxLoader::VoxData> VoxLoader::LoadVox(const char* filename) {
    if (!filename) {
        spdlog::error("VoxLoader: filename is null");
        return nullptr;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        spdlog::error("VoxLoader: Failed to open file {}", filename);
        return nullptr;
    }
    
    auto data = std::make_shared<VoxData>();
    bool success = false;
    
    try {
        VoxHeader header;
        if (!ReadHeader(file, header)) {
            spdlog::error("VoxLoader: Invalid .vox file header in {}", filename);
            fclose(file);
            return nullptr;
        }
        
        // 读取主块
        ChunkHeader main_header;
        if (!ReadChunkHeader(file, main_header) || main_header.id != CHUNK_MAIN) {
            spdlog::error("VoxLoader: Missing or invalid MAIN chunk in {}", filename);
            fclose(file);
            return nullptr;
        }
        
        success = ParseMainChunk(file, main_header, *data);
        
        if (success && data->palette.empty()) {
            SetDefaultPalette(*data);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("VoxLoader: Exception while parsing {}: {}", filename, e.what());
        success = false;
    }
    
    fclose(file);
    
    if (!success) {
        spdlog::error("VoxLoader: Failed to parse .vox file {}", filename);
        return nullptr;
    }
    
    spdlog::info("VoxLoader: Successfully loaded {} voxels from {}", 
                 data->GetVoxelCount(), filename);
    
    return data;
}

bool VoxLoader::IsVoxFile(const char* filename) {
    if (!filename) return false;
    
    std::string ext = GetFileExtension(filename);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".vox";
}

bool VoxLoader::ReadHeader(FILE* file, VoxHeader& header) {
    if (fread(&header, sizeof(VoxHeader), 1, file) != 1) {
        return false;
    }
    
    // 检查魔数和版本
    if (header.magic != VOX_MAGIC) {
        spdlog::error("VoxLoader: Invalid magic number: 0x{:08X}, expected 0x{:08X}", 
                      header.magic, VOX_MAGIC);
        return false;
    }
    
    if (header.version > VOX_VERSION) {
        spdlog::info("VoxLoader: File version {} is newer than tested version {}, attempting to load anyway",
                     header.version, VOX_VERSION);
    }
    
    // 确保版本至少是我们能处理的最低版本
    if (header.version < 150) {
        spdlog::error("VoxLoader: File version {} is too old, minimum supported version is 150",
                      header.version);
        return false;
    }
    
    return true;
}

bool VoxLoader::ReadChunkHeader(FILE* file, ChunkHeader& chunk_header) {
    return fread(&chunk_header, sizeof(ChunkHeader), 1, file) == 1;
}

bool VoxLoader::ParseMainChunk(FILE* file, const ChunkHeader& main_header, VoxData& data) {
    long main_start = ftell(file);
    long main_end = main_start + main_header.child_size;
    
    bool found_size = false, found_xyzi = false;
    
    while (ftell(file) < main_end) {
        ChunkHeader chunk_header;
        if (!ReadChunkHeader(file, chunk_header)) {
            spdlog::error("VoxLoader: Failed to read chunk header");
            return false;
        }
        
        switch (chunk_header.id) {
            case CHUNK_SIZE:
                if (!ParseSizeChunk(file, chunk_header, data)) {
                    return false;
                }
                found_size = true;
                break;
                
            case CHUNK_XYZI:
                if (!ParseXyziChunk(file, chunk_header, data)) {
                    return false;
                }
                found_xyzi = true;
                break;
                
            case CHUNK_RGBA:
                if (!ParseRgbaChunk(file, chunk_header, data)) {
                    return false;
                }
                break;
                
            default:
                // 跳过未知块
                fseek(file, chunk_header.content_size + chunk_header.child_size, SEEK_CUR);
                break;
        }
    }
    
    if (!found_size || !found_xyzi) {
        spdlog::error("VoxLoader: Missing required SIZE or XYZI chunk");
        return false;
    }
    
    return true;
}

bool VoxLoader::ParseSizeChunk(FILE* file, const ChunkHeader& size_header, VoxData& data) {
    if (size_header.content_size < 12) {
        spdlog::error("VoxLoader: SIZE chunk too small");
        return false;
    }
    
    uint32_t x, y, z;
    if (fread(&x, 4, 1, file) != 1 ||
        fread(&y, 4, 1, file) != 1 ||
        fread(&z, 4, 1, file) != 1) {
        spdlog::error("VoxLoader: Failed to read SIZE data");
        return false;
    }
    
    data.dimensions = glm::ivec3(x, y, z);
    
    // 跳过SIZE块的其余内容
    if (size_header.content_size > 12) {
        fseek(file, size_header.content_size - 12, SEEK_CUR);
    }
    
    // 跳过子块
    fseek(file, size_header.child_size, SEEK_CUR);
    
    spdlog::debug("VoxLoader: Model dimensions: {}x{}x{}", x, y, z);
    return true;
}

bool VoxLoader::ParseXyziChunk(FILE* file, const ChunkHeader& xyzi_header, VoxData& data) {
    if (xyzi_header.content_size < 4) {
        spdlog::error("VoxLoader: XYZI chunk too small");
        return false;
    }
    
    uint32_t voxel_count;
    if (fread(&voxel_count, 4, 1, file) != 1) {
        spdlog::error("VoxLoader: Failed to read voxel count");
        return false;
    }
    
    if (xyzi_header.content_size < 4 + voxel_count * 4) {
        spdlog::error("VoxLoader: XYZI chunk size mismatch");
        return false;
    }
    
    data.voxels.reserve(voxel_count);
    
    for (uint32_t i = 0; i < voxel_count; ++i) {
        uint8_t x, y, z, color_index;
        if (fread(&x, 1, 1, file) != 1 ||
            fread(&y, 1, 1, file) != 1 ||
            fread(&z, 1, 1, file) != 1 ||
            fread(&color_index, 1, 1, file) != 1) {
            spdlog::error("VoxLoader: Failed to read voxel data");
            return false;
        }
        
        VoxelData voxel;
        voxel.position = glm::u8vec3(x, y, z);
        voxel.color_index = color_index;
        data.voxels.push_back(voxel);
    }
    
    // 跳过XYZI块的其余内容
    if (xyzi_header.content_size > 4 + voxel_count * 4) {
        fseek(file, xyzi_header.content_size - 4 - voxel_count * 4, SEEK_CUR);
    }
    
    // 跳过子块
    fseek(file, xyzi_header.child_size, SEEK_CUR);
    
    spdlog::debug("VoxLoader: Loaded {} voxels", voxel_count);
    return true;
}

bool VoxLoader::ParseRgbaChunk(FILE* file, const ChunkHeader& rgba_header, VoxData& data) {
    if (rgba_header.content_size < 1024) { // 256 colors * 4 bytes
        spdlog::error("VoxLoader: RGBA chunk too small");
        return false;
    }
    
    data.palette.resize(256);
    
    for (int i = 0; i < 256; ++i) {
        uint8_t r, g, b, a;
        if (fread(&r, 1, 1, file) != 1 ||
            fread(&g, 1, 1, file) != 1 ||
            fread(&b, 1, 1, file) != 1 ||
            fread(&a, 1, 1, file) != 1) {
            spdlog::error("VoxLoader: Failed to read palette data");
            return false;
        }
        data.palette[i] = glm::u8vec4(r, g, b, a);
    }
    
    // 跳过RGBA块的其余内容
    if (rgba_header.content_size > 1024) {
        fseek(file, rgba_header.content_size - 1024, SEEK_CUR);
    }
    
    // 跳过子块
    fseek(file, rgba_header.child_size, SEEK_CUR);
    
    spdlog::debug("VoxLoader: Loaded custom palette");
    return true;
}

void VoxLoader::SetDefaultPalette(VoxData& data) {
    data.palette.resize(256);
    
    // 复制预定义的默认调色板
    for (int i = 0; i < 16; ++i) {
        data.palette[i] = DEFAULT_PALETTE[i];
    }
    
    // 为其余颜色生成渐变
    for (int i = 16; i < 256; ++i) {
        float t = static_cast<float>(i - 16) / (256 - 16);
        
        // 使用HSV到RGB的简单转换创建彩虹渐变
        float hue = t * 360.0f;
        float saturation = 1.0f;
        float value = 1.0f;
        
        float c = value * saturation;
        float x = c * (1.0f - std::abs(fmod(hue / 60.0f, 2.0f) - 1.0f));
        float m = value - c;
        
        float r, g, b;
        if (hue < 60) {
            r = c; g = x; b = 0;
        } else if (hue < 120) {
            r = x; g = c; b = 0;
        } else if (hue < 180) {
            r = 0; g = c; b = x;
        } else if (hue < 240) {
            r = 0; g = x; b = c;
        } else if (hue < 300) {
            r = x; g = 0; b = c;
        } else {
            r = c; g = 0; b = x;
        }
        
        data.palette[i] = glm::u8vec4(
            static_cast<uint8_t>((r + m) * 255),
            static_cast<uint8_t>((g + m) * 255),
            static_cast<uint8_t>((b + m) * 255),
            255
        );
    }
    
    spdlog::debug("VoxLoader: Applied default palette");
}

std::string VoxLoader::GetFileExtension(const char* filename) {
    std::string fname(filename);
    size_t dot_pos = fname.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "";
    }
    return fname.substr(dot_pos);
}