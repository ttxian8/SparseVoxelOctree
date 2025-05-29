#ifndef VOXLOADER_HPP
#define VOXLOADER_HPP

#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>

/**
 * VoxLoader - MagicaVoxel .vox文件加载器
 * 支持解析.vox格式文件并提供体素数据
 */
class VoxLoader {
public:
    // 体素数据结构
    struct VoxelData {
        glm::u8vec3 position;  // 体素位置 (x, y, z)
        uint8_t color_index;   // 调色板颜色索引
    };

    // .vox文件数据结构
    struct VoxData {
        glm::ivec3 dimensions;                    // 模型尺寸 (width, height, depth)
        std::vector<VoxelData> voxels;           // 体素数据数组
        std::vector<glm::u8vec4> palette;        // 调色板 (RGBA)
        std::string name;                        // 模型名称（如果有）
        
        VoxData() : dimensions(0, 0, 0) {}
        
        // 便利方法：检查是否为空
        bool IsEmpty() const { return voxels.empty(); }
        
        // 便利方法：获取体素总数
        size_t GetVoxelCount() const { return voxels.size(); }
    };

    /**
     * 加载.vox文件
     * @param filename .vox文件路径
     * @return 解析后的体素数据，失败返回nullptr
     */
    static std::shared_ptr<VoxData> LoadVox(const char* filename);

    /**
     * 检查文件是否为有效的.vox格式
     * @param filename 文件路径
     * @return true表示是有效的.vox文件
     */
    static bool IsVoxFile(const char* filename);

private:
    // .vox文件格式常量
    static constexpr uint32_t VOX_MAGIC = 0x20584F56;  // 'VOX ' 
    static constexpr uint32_t VOX_VERSION = 200;       // 支持的版本

    // 块类型标识符
    static constexpr uint32_t CHUNK_MAIN = 0x4E49414D;  // 'MAIN'
    static constexpr uint32_t CHUNK_SIZE = 0x455A4953;  // 'SIZE'
    static constexpr uint32_t CHUNK_XYZI = 0x495A5958;  // 'XYZI'
    static constexpr uint32_t CHUNK_RGBA = 0x41424752;  // 'RGBA'

    // 内部数据结构
    struct VoxHeader {
        uint32_t magic;
        uint32_t version;
    };

    struct ChunkHeader {
        uint32_t id;           // 块标识符
        uint32_t content_size; // 内容大小
        uint32_t child_size;   // 子块大小
    };

    // 内部解析方法
    static bool ReadHeader(FILE* file, VoxHeader& header);
    static bool ReadChunkHeader(FILE* file, ChunkHeader& chunk_header);
    static bool ParseMainChunk(FILE* file, const ChunkHeader& main_header, VoxData& data);
    static bool ParseSizeChunk(FILE* file, const ChunkHeader& size_header, VoxData& data);
    static bool ParseXyziChunk(FILE* file, const ChunkHeader& xyzi_header, VoxData& data);
    static bool ParseRgbaChunk(FILE* file, const ChunkHeader& rgba_header, VoxData& data);
    
    // 辅助方法
    static void SetDefaultPalette(VoxData& data);
    static std::string GetFileExtension(const char* filename);
    
    // 禁用构造函数 - 纯静态类
    VoxLoader() = delete;
    VoxLoader(const VoxLoader&) = delete;
    VoxLoader& operator=(const VoxLoader&) = delete;
};

#endif // VOXLOADER_HPP