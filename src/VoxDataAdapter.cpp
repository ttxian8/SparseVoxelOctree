#include "VoxDataAdapter.hpp"
#include "Config.hpp"
#include "myvk/CommandBuffer.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <climits>

std::shared_ptr<VoxDataAdapter> VoxDataAdapter::Create(const VoxLoader::VoxData &vox_data,
                                                      const std::shared_ptr<myvk::Device> &device,
                                                      const std::shared_ptr<myvk::CommandPool> &command_pool,
                                                      uint32_t octree_level) {
	auto ret = std::make_shared<VoxDataAdapter>();
	
	ret->m_level = octree_level;
	ret->m_voxel_resolution = 1u << octree_level;
	
	ret->create_fragment_list_from_vox_data(device, command_pool, vox_data);
	
	spdlog::info("VoxDataAdapter created: level={}, resolution={}, fragments={}",
	             ret->m_level, ret->m_voxel_resolution, ret->m_voxel_fragment_count);
	
	return ret;
}

void VoxDataAdapter::create_fragment_list_from_vox_data(const std::shared_ptr<myvk::Device> &device,
                                                       const std::shared_ptr<myvk::CommandPool> &command_pool,
                                                       const VoxLoader::VoxData &vox_data) {
	// 计算有效体素数量
	std::vector<uint32_t> fragment_data;
	m_voxel_fragment_count = 0;
	
	// 找到体素数据的边界（MagicaVoxel原始坐标）
	int min_x = INT_MAX, min_y = INT_MAX, min_z = INT_MAX;
	int max_x = INT_MIN, max_y = INT_MIN, max_z = INT_MIN;
	
	for (const auto &voxel : vox_data.voxels) {
		min_x = std::min<int>(min_x, static_cast<int>(voxel.position.x));
		min_y = std::min<int>(min_y, static_cast<int>(voxel.position.y));
		min_z = std::min<int>(min_z, static_cast<int>(voxel.position.z));
		max_x = std::max<int>(max_x, static_cast<int>(voxel.position.x));
		max_y = std::max<int>(max_y, static_cast<int>(voxel.position.y));
		max_z = std::max<int>(max_z, static_cast<int>(voxel.position.z));
	}
	
	// 计算源数据尺寸（按MagicaVoxel坐标）
	int size_x = max_x - min_x + 1;
	int size_y = max_y - min_y + 1;
	int size_z = max_z - min_z + 1;
	int max_size = std::max({size_x, size_y, size_z});
	
	// 使用保持密度的缩放：不要填满整个分辨率空间，保持原有密度
	float voxel_scale = std::min(1.0f, static_cast<float>(m_voxel_resolution / 4) / max_size);
	
	// 居中偏移（注意：要按照映射后的坐标轴计算偏移）
	uint32_t offset_x = (m_voxel_resolution - static_cast<uint32_t>(size_x * voxel_scale)) / 2;
	uint32_t offset_y = (m_voxel_resolution - static_cast<uint32_t>(size_z * voxel_scale)) / 2;  // Z->Y
	uint32_t offset_z = (m_voxel_resolution - static_cast<uint32_t>(size_y * voxel_scale)) / 2;  // Y->Z
	
	spdlog::info("Vox data bounds: ({},{},{}) to ({},{},{}), size: {}x{}x{}, scale: {}, voxel count: {}",
	             min_x, min_y, min_z, max_x, max_y, max_z, size_x, size_y, size_z, voxel_scale, vox_data.voxels.size());
	spdlog::info("Target resolution: {}, offsets: ({},{},{})",
	             m_voxel_resolution, offset_x, offset_y, offset_z);
	
	// 转换体素到fragment格式 - 修复坐标系映射
	for (const auto &voxel : vox_data.voxels) {
		// MagicaVoxel: X(右), Y(后), Z(上) -> 渲染引擎: X(右), Y(上), Z(后)
		// 需要交换Y和Z轴来修正方向
		uint32_t x = static_cast<uint32_t>((voxel.position.x - min_x) * voxel_scale) + offset_x;
		uint32_t y = static_cast<uint32_t>((voxel.position.z - min_z) * voxel_scale) + offset_y;  // Z->Y
		uint32_t z = static_cast<uint32_t>((voxel.position.y - min_y) * voxel_scale) + offset_z;  // Y->Z
		
		// 确保坐标在有效范围内
		x = std::min(x, m_voxel_resolution - 1);
		y = std::min(y, m_voxel_resolution - 1);
		z = std::min(z, m_voxel_resolution - 1);
		
		// 获取颜色信息
		uint32_t color_rgb = 0x000000; // 默认黑色 (无alpha)
		if (voxel.color_index > 0 && voxel.color_index <= vox_data.palette.size()) {
			const auto &color = vox_data.palette[voxel.color_index - 1];
			color_rgb = (color.r << 16) | (color.g << 8) | color.b; // RGB only, 24位
		}
		
		// 使用与Voxelizer shader相同的格式
		// 第一个uint32: x(12位) | y(12位) | z低8位(8位)
		uint32_t fragment_x = (x & 0xFFF) | ((y & 0xFFF) << 12) | ((z & 0xFF) << 24);
		// 第二个uint32: z高4位(4位) | RGB颜色(24位)
		uint32_t fragment_y = ((z >> 8) << 28) | (color_rgb & 0x00FFFFFF);
		
		// 添加fragment数据：匹配shader期望的uvec2格式
		fragment_data.push_back(fragment_x);
		fragment_data.push_back(fragment_y);
		
		m_voxel_fragment_count++;
		
		// 添加调试输出前几个体素的信息
		if (m_voxel_fragment_count <= 5) {
			spdlog::info("Voxel {}: orig({},{},{}) -> scaled({},{},{}) -> fragments(0x{:08X}, 0x{:08X})",
				m_voxel_fragment_count, voxel.position.x, voxel.position.y, voxel.position.z,
				x, y, z, fragment_x, fragment_y);
		}
	}
	
	if (fragment_data.empty()) {
		spdlog::warn("No valid voxels found in .vox file");
		// 创建一个最小的缓冲区以避免错误
		fragment_data.push_back(0);
		fragment_data.push_back(0xFF000000);
		m_voxel_fragment_count = 1;
	}
	
	// 创建Vulkan缓冲区
	VkDeviceSize buffer_size = fragment_data.size() * sizeof(uint32_t);
	
	// 先创建staging buffer用于数据传输
	auto staging_buffer = myvk::Buffer::CreateStaging<uint32_t>(device, fragment_data.size(),
	                                                           [&fragment_data](uint32_t *data) {
		                                                           std::copy(fragment_data.begin(), fragment_data.end(), data);
	                                                           });
	
	// 创建设备本地存储缓冲区
	m_voxel_fragment_list = myvk::Buffer::Create(device, buffer_size, 0,
	                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	
	// 创建命令缓冲区并执行数据复制
	auto command_buffer = myvk::CommandBuffer::Create(command_pool);
	command_buffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	
	// 复制数据从staging buffer到设备buffer
	VkBufferCopy copy_region{};
	copy_region.size = buffer_size;
	command_buffer->CmdCopy(staging_buffer, m_voxel_fragment_list, {copy_region});
	
	command_buffer->End();
	
	// 执行复制命令并等待完成
	auto fence = myvk::Fence::Create(device);
	command_buffer->Submit(fence);
	fence->Wait();
	
	spdlog::info("Created voxel fragment buffer with {} fragments ({} bytes)",
	             m_voxel_fragment_count, buffer_size);
}