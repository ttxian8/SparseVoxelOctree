#ifndef VOX_DATA_ADAPTER_HPP
#define VOX_DATA_ADAPTER_HPP

#include "VoxLoader.hpp"
#include "myvk/Buffer.hpp"
#include "myvk/CommandPool.hpp"
#include "myvk/Device.hpp"

class VoxDataAdapter {
private:
	uint32_t m_level;
	uint32_t m_voxel_resolution;
	uint32_t m_voxel_fragment_count;
	std::shared_ptr<myvk::Buffer> m_voxel_fragment_list;

	void create_fragment_list_from_vox_data(const std::shared_ptr<myvk::Device> &device,
	                                       const std::shared_ptr<myvk::CommandPool> &command_pool,
	                                       const VoxLoader::VoxData &vox_data);

public:
	static std::shared_ptr<VoxDataAdapter> Create(const VoxLoader::VoxData &vox_data,
	                                               const std::shared_ptr<myvk::Device> &device,
	                                               const std::shared_ptr<myvk::CommandPool> &command_pool,
	                                               uint32_t octree_level);

	uint32_t GetLevel() const { return m_level; }
	uint32_t GetVoxelResolution() const { return m_voxel_resolution; }
	uint32_t GetVoxelFragmentCount() const { return m_voxel_fragment_count; }
	const std::shared_ptr<myvk::Buffer> &GetVoxelFragmentList() const { return m_voxel_fragment_list; }
};

#endif