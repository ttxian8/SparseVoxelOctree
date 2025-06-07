#include "OctreeBuilder.hpp"
#include "Config.hpp"
#include "Voxelizer.hpp"
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <vector>
#include <mutex>
#include <cstring>

inline static constexpr uint32_t group_x_64(uint32_t x) { return (x >> 6u) + ((x & 0x3fu) ? 1u : 0u); }

std::shared_ptr<OctreeBuilder> OctreeBuilder::Create(const std::shared_ptr<Voxelizer> &voxelizer,
                                                     const std::shared_ptr<myvk::CommandPool> &command_pool) {
	std::shared_ptr<OctreeBuilder> ret = std::make_shared<OctreeBuilder>();

	std::shared_ptr<myvk::Device> device = command_pool->GetDevicePtr();
	ret->m_voxelizer_ptr = voxelizer;
	ret->m_atomic_counter.Initialize(device);
	ret->m_atomic_counter.Reset(command_pool, 0);

	ret->create_buffers(device);
	ret->create_descriptors(device);
	ret->create_pipeline(device);

	return ret;
}

std::shared_ptr<OctreeBuilder> OctreeBuilder::Create(const std::shared_ptr<VoxDataAdapter> &vox_adapter,
                                                     const std::shared_ptr<myvk::CommandPool> &command_pool) {
	std::shared_ptr<OctreeBuilder> ret = std::make_shared<OctreeBuilder>();

	std::shared_ptr<myvk::Device> device = command_pool->GetDevicePtr();
	ret->m_vox_adapter_ptr = vox_adapter;
	ret->m_atomic_counter.Initialize(device);
	ret->m_atomic_counter.Reset(command_pool, 0);

	ret->create_buffers_from_vox(device);
	ret->create_descriptors_from_vox(device);
	ret->create_pipeline_from_vox(device);

	return ret;
}

void OctreeBuilder::create_buffers(const std::shared_ptr<myvk::Device> &device) {
	m_build_info_buffer = myvk::Buffer::Create(device, 2 * sizeof(uint32_t), 0,
	                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	m_build_info_staging_buffer = myvk::Buffer::CreateStaging<uint32_t>(device, 2, [](uint32_t *data) {
		data[0] = 0; // uAllocBegin
		data[1] = 8; // uAllocNum
	});

	m_indirect_buffer = myvk::Buffer::Create(device, 3 * sizeof(uint32_t), 0,
	                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
	                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	m_indirect_staging_buffer = myvk::Buffer::CreateStaging<uint32_t>(device, 3, [](uint32_t *data) {
		data[0] = 1; // uGroupX
		data[1] = 1; // uGroupY
		data[2] = 1; // uGroupZ
	});

	// Estimate octree buffer size
	uint32_t octree_node_ratio = m_voxelizer_ptr->GetLevel() / 3;
	uint32_t octree_entry_num =
	    std::max(kOctreeNodeNumMin, m_voxelizer_ptr->GetVoxelFragmentCount() * octree_node_ratio);
	octree_entry_num = std::min(octree_entry_num, kOctreeNodeNumMax);

	m_octree_buffer =
	    myvk::Buffer::Create(device, octree_entry_num * sizeof(uint32_t), 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	spdlog::info("Octree buffer created with {} nodes ({} MB)", octree_entry_num,
	             m_octree_buffer->GetSize() / 1000000.0);
}

void OctreeBuilder::create_descriptors(const std::shared_ptr<myvk::Device> &device) {
	m_descriptor_pool = myvk::DescriptorPool::Create(device, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}});
	{
		VkDescriptorSetLayoutBinding atomic_counter_binding = {};
		atomic_counter_binding.binding = 0;
		atomic_counter_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		atomic_counter_binding.descriptorCount = 1;
		atomic_counter_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding octree_binding = {};
		octree_binding.binding = 1;
		octree_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		octree_binding.descriptorCount = 1;
		octree_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding fragment_list_binding = {};
		fragment_list_binding.binding = 2;
		fragment_list_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		fragment_list_binding.descriptorCount = 1;
		fragment_list_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding build_info_binding = {};
		build_info_binding.binding = 3;
		build_info_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		build_info_binding.descriptorCount = 1;
		build_info_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding indirect_binding = {};
		indirect_binding.binding = 4;
		indirect_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_binding.descriptorCount = 1;
		indirect_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		m_descriptor_set_layout =
		    myvk::DescriptorSetLayout::Create(device, {atomic_counter_binding, octree_binding, fragment_list_binding,
		                                               build_info_binding, indirect_binding});
	}
	m_descriptor_set = myvk::DescriptorSet::Create(m_descriptor_pool, m_descriptor_set_layout);
	m_descriptor_set->UpdateStorageBuffer(m_atomic_counter.GetBuffer(), 0);
	m_descriptor_set->UpdateStorageBuffer(m_octree_buffer, 1);
	m_descriptor_set->UpdateStorageBuffer(m_voxelizer_ptr->GetVoxelFragmentList(), 2);
	m_descriptor_set->UpdateStorageBuffer(m_build_info_buffer, 3);
	m_descriptor_set->UpdateStorageBuffer(m_indirect_buffer, 4);
}

void OctreeBuilder::create_pipeline(const std::shared_ptr<myvk::Device> &device) {
	m_pipeline_layout = myvk::PipelineLayout::Create(device, {m_descriptor_set_layout}, {});

	{
		uint32_t spec_data[] = {m_voxelizer_ptr->GetVoxelResolution(), m_voxelizer_ptr->GetVoxelFragmentCount()};
		VkSpecializationMapEntry spec_entries[] = {{0, 0, sizeof(uint32_t)}, {1, sizeof(uint32_t), sizeof(uint32_t)}};
		VkSpecializationInfo spec_info = {2, spec_entries, 2 * sizeof(uint32_t), spec_data};
		constexpr uint32_t kOctreeTagNodeCompSpv[] = {
#include "spirv/octree_tag_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_tag_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeTagNodeCompSpv, sizeof(kOctreeTagNodeCompSpv));
		m_tag_node_pipeline =
		    myvk::ComputePipeline::Create(m_pipeline_layout, octree_tag_node_shader_module, &spec_info);
	}

	{
		constexpr uint32_t kOctreeInitNodeCompSpv[] = {
#include "spirv/octree_init_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_init_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeInitNodeCompSpv, sizeof(kOctreeInitNodeCompSpv));
		m_init_node_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_init_node_shader_module);
	}

	{
		constexpr uint32_t kOctreeAllocNodeCompSpv[] = {
#include "spirv/octree_alloc_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_alloc_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeAllocNodeCompSpv, sizeof(kOctreeAllocNodeCompSpv));
		m_alloc_node_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_alloc_node_shader_module);
	}

	{
		constexpr uint32_t kOctreeModifyArgCompSpv[] = {
#include "spirv/octree_modify_arg.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_modify_arg_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeModifyArgCompSpv, sizeof(kOctreeModifyArgCompSpv));
		m_modify_arg_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_modify_arg_shader_module);
	}
}

void OctreeBuilder::create_buffers_from_vox(const std::shared_ptr<myvk::Device> &device) {
	m_build_info_buffer = myvk::Buffer::Create(device, 2 * sizeof(uint32_t), 0,
	                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	m_build_info_staging_buffer = myvk::Buffer::CreateStaging<uint32_t>(device, 2, [](uint32_t *data) {
		data[0] = 0; // uAllocBegin
		data[1] = 8; // uAllocNum
	});

	m_indirect_buffer = myvk::Buffer::Create(device, 3 * sizeof(uint32_t), 0,
	                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
	                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	m_indirect_staging_buffer = myvk::Buffer::CreateStaging<uint32_t>(device, 3, [](uint32_t *data) {
		data[0] = 1; // uGroupX
		data[1] = 1; // uGroupY
		data[2] = 1; // uGroupZ
	});

	// 基于VoxDataAdapter估算八叉树缓冲区大小
	// .vox文件内存预估：参考OBJ流程，使用合理的倍数
	uint32_t octree_node_ratio = m_vox_adapter_ptr->GetLevel() / 3;  // 比OBJ的level/3稍大
	octree_node_ratio = std::max(8u, octree_node_ratio);  // 最小倍数保证
	uint32_t octree_entry_num =
	    std::max(kOctreeNodeNumMin, m_vox_adapter_ptr->GetVoxelFragmentCount() * octree_node_ratio);
	octree_entry_num = std::min(octree_entry_num, kOctreeNodeNumMax);
	
	spdlog::info("VoxAdapter memory estimation: fragments={}, ratio={}, estimated_nodes={}",
	             m_vox_adapter_ptr->GetVoxelFragmentCount(), octree_node_ratio, octree_entry_num);

	m_octree_buffer =
	    myvk::Buffer::Create(device, octree_entry_num * sizeof(uint32_t), 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	spdlog::info("Octree buffer created from .vox with {} nodes ({} MB)", octree_entry_num,
	             m_octree_buffer->GetSize() / 1000000.0);
}

void OctreeBuilder::create_descriptors_from_vox(const std::shared_ptr<myvk::Device> &device) {
	m_descriptor_pool = myvk::DescriptorPool::Create(device, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}});
	{
		VkDescriptorSetLayoutBinding atomic_counter_binding = {};
		atomic_counter_binding.binding = 0;
		atomic_counter_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		atomic_counter_binding.descriptorCount = 1;
		atomic_counter_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding octree_binding = {};
		octree_binding.binding = 1;
		octree_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		octree_binding.descriptorCount = 1;
		octree_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding fragment_list_binding = {};
		fragment_list_binding.binding = 2;
		fragment_list_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		fragment_list_binding.descriptorCount = 1;
		fragment_list_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding build_info_binding = {};
		build_info_binding.binding = 3;
		build_info_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		build_info_binding.descriptorCount = 1;
		build_info_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutBinding indirect_binding = {};
		indirect_binding.binding = 4;
		indirect_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		indirect_binding.descriptorCount = 1;
		indirect_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		m_descriptor_set_layout =
		    myvk::DescriptorSetLayout::Create(device, {atomic_counter_binding, octree_binding, fragment_list_binding,
		                                               build_info_binding, indirect_binding});
	}
	m_descriptor_set = myvk::DescriptorSet::Create(m_descriptor_pool, m_descriptor_set_layout);
	m_descriptor_set->UpdateStorageBuffer(m_atomic_counter.GetBuffer(), 0);
	m_descriptor_set->UpdateStorageBuffer(m_octree_buffer, 1);
	m_descriptor_set->UpdateStorageBuffer(m_vox_adapter_ptr->GetVoxelFragmentList(), 2);
	m_descriptor_set->UpdateStorageBuffer(m_build_info_buffer, 3);
	m_descriptor_set->UpdateStorageBuffer(m_indirect_buffer, 4);
}

void OctreeBuilder::create_pipeline_from_vox(const std::shared_ptr<myvk::Device> &device) {
	m_pipeline_layout = myvk::PipelineLayout::Create(device, {m_descriptor_set_layout}, {});

	{
		uint32_t spec_data[] = {m_vox_adapter_ptr->GetVoxelResolution(), m_vox_adapter_ptr->GetVoxelFragmentCount()};
		VkSpecializationMapEntry spec_entries[] = {{0, 0, sizeof(uint32_t)}, {1, sizeof(uint32_t), sizeof(uint32_t)}};
		VkSpecializationInfo spec_info = {2, spec_entries, 2 * sizeof(uint32_t), spec_data};
		constexpr uint32_t kOctreeTagNodeCompSpv[] = {
#include "spirv/octree_tag_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_tag_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeTagNodeCompSpv, sizeof(kOctreeTagNodeCompSpv));
		m_tag_node_pipeline =
		    myvk::ComputePipeline::Create(m_pipeline_layout, octree_tag_node_shader_module, &spec_info);
	}

	{
		constexpr uint32_t kOctreeInitNodeCompSpv[] = {
#include "spirv/octree_init_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_init_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeInitNodeCompSpv, sizeof(kOctreeInitNodeCompSpv));
		m_init_node_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_init_node_shader_module);
	}

	{
		constexpr uint32_t kOctreeAllocNodeCompSpv[] = {
#include "spirv/octree_alloc_node.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_alloc_node_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeAllocNodeCompSpv, sizeof(kOctreeAllocNodeCompSpv));
		m_alloc_node_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_alloc_node_shader_module);
	}

	{
		constexpr uint32_t kOctreeModifyArgCompSpv[] = {
#include "spirv/octree_modify_arg.comp.u32"
		};
		std::shared_ptr<myvk::ShaderModule> octree_modify_arg_shader_module =
		    myvk::ShaderModule::Create(device, kOctreeModifyArgCompSpv, sizeof(kOctreeModifyArgCompSpv));
		m_modify_arg_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, octree_modify_arg_shader_module);
	}
}

void OctreeBuilder::CmdBuild(const std::shared_ptr<myvk::CommandBuffer> &command_buffer) const {
	// transfers
	{
		command_buffer->CmdCopy(m_build_info_staging_buffer, m_build_info_buffer,
		                        {{0, 0, m_build_info_buffer->GetSize()}});
		command_buffer->CmdCopy(m_indirect_staging_buffer, m_indirect_buffer, {{0, 0, m_indirect_buffer->GetSize()}});

		command_buffer->CmdPipelineBarrier(
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
		    {m_build_info_buffer->GetMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
		                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
		    {});

		command_buffer->CmdPipelineBarrier(
		    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		    {},
		    {m_indirect_buffer->GetMemoryBarrier(VK_ACCESS_TRANSFER_WRITE_BIT,
		                                         VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
		    {});
	}

	uint32_t fragment_count = 0;
	uint32_t octree_level = 0;
	
	if (m_voxelizer_ptr) {
		fragment_count = m_voxelizer_ptr->GetVoxelFragmentCount();
		octree_level = m_voxelizer_ptr->GetLevel();
	} else if (m_vox_adapter_ptr) {
		fragment_count = m_vox_adapter_ptr->GetVoxelFragmentCount();
		octree_level = m_vox_adapter_ptr->GetLevel();
	}
	
	uint32_t fragment_group_x = group_x_64(fragment_count);

	command_buffer->CmdBindDescriptorSets({m_descriptor_set}, m_pipeline_layout, VK_PIPELINE_BIND_POINT_COMPUTE, {});

	for (uint32_t i = 1; i <= octree_level; ++i) {
		command_buffer->CmdBindPipeline(m_init_node_pipeline);
		command_buffer->CmdDispatchIndirect(m_indirect_buffer);

		command_buffer->CmdPipelineBarrier(
		    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
		    {m_octree_buffer->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT,
		                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
		    {});

		command_buffer->CmdBindPipeline(m_tag_node_pipeline);
		command_buffer->CmdDispatch(fragment_group_x, 1, 1);

		if (i != octree_level) {
			command_buffer->CmdPipelineBarrier(
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
			    {m_octree_buffer->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT,
			                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
			    {});

			command_buffer->CmdBindPipeline(m_alloc_node_pipeline);
			command_buffer->CmdDispatchIndirect(m_indirect_buffer);

			command_buffer->CmdPipelineBarrier(
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
			    {m_octree_buffer->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT,
			                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
			    {});

			command_buffer->CmdBindPipeline(m_modify_arg_pipeline);
			command_buffer->CmdDispatch(1, 1, 1);

			command_buffer->CmdPipelineBarrier(
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
			    {m_indirect_buffer->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT,
			                                         VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)},
			    {});
			command_buffer->CmdPipelineBarrier(
			    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {},
			    {m_build_info_buffer->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)}, {});
		}
	}
}

VkDeviceSize OctreeBuilder::GetOctreeRange(const std::shared_ptr<myvk::CommandPool> &command_pool) const {
	return (m_atomic_counter.Read(command_pool) + 1u) * 8u * sizeof(uint32_t);
}
void OctreeBuilder::CmdTransferOctreeOwnership(const std::shared_ptr<myvk::CommandBuffer> &command_buffer,
                                               uint32_t src_queue_family, uint32_t dst_queue_family,
                                               VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) const {
	command_buffer->CmdPipelineBarrier(
	    src_stage, dst_stage, {}, {m_octree_buffer->GetMemoryBarrier(0, 0, src_queue_family, dst_queue_family)}, {});
}

// 新增：体素区域销毁（球形，实际实现需遍历voxel数据）
// 这里只提供伪实现，你需针对自身体素数据结构优化！
// 可用多线程或并行处理大范围操作
void OctreeBuilder::RemoveVoxelsRegion(const glm::vec3 &center, float radius) {
	// 体素分辨率
	if (!m_voxelizer_ptr) return;
	uint32_t res = m_voxelizer_ptr->GetVoxelResolution();
	auto voxel_buffer = m_voxelizer_ptr->GetVoxelFragmentList();
	if (!voxel_buffer) return;

	uint32_t voxel_count = m_voxelizer_ptr->GetVoxelFragmentCount();
	if (voxel_count == 0) return;

	// 体素坐标解包函数
	auto unpack_voxel_coord = [res](uint32_t packed) -> glm::ivec3 {
		return glm::ivec3(
			int((packed >>  0) & 0x3FF),
			int((packed >> 10) & 0x3FF),
			int((packed >> 20) & 0x3FF)
		);
	};
	auto voxel_to_world = [res](const glm::ivec3& v) -> glm::vec3 {
		return glm::vec3(v) / float(res);
	};

	// 读取现有体素数据到CPU
	std::vector<uint32_t> temp_fragments(voxel_count * 2);
	void* mapped = voxel_buffer->Map();
	if (!mapped) {
		spdlog::error("Voxel buffer map failed!");
		return;
	}
	std::memcpy(temp_fragments.data(), mapped, temp_fragments.size() * sizeof(uint32_t));
	voxel_buffer->Unmap();

	// 新建输出体素表
	std::vector<uint32_t> new_fragments;
	new_fragments.reserve(temp_fragments.size());

	uint32_t removed = 0;
	for (uint32_t i = 0; i < voxel_count; ++i) {
		uint32_t packed = temp_fragments[i * 2];
		glm::ivec3 vox = unpack_voxel_coord(packed);
		glm::vec3 world_pos = voxel_to_world(vox);

		if (glm::distance(world_pos, center) >= radius) {
			new_fragments.push_back(temp_fragments[i * 2]);
			new_fragments.push_back(temp_fragments[i * 2 + 1]);
		} else {
			++removed;
		}
	}

	if (removed > 0) {
		spdlog::info("Voxel destruction: removed {} voxels at ({},{},{}) radius {}", removed, center.x, center.y, center.z, radius);

		// 更新缓冲区
		void* map_update = voxel_buffer->Map();
		if (map_update) {
			std::memcpy(map_update, new_fragments.data(), new_fragments.size() * sizeof(uint32_t));
			voxel_buffer->Unmap();
		} else {
			spdlog::error("Voxel buffer map for update failed!");
		}

		// 更新计数
		m_voxelizer_ptr->SetVoxelFragmentCount(uint32_t(new_fragments.size() / 2));

		// 标记需要重建octree（主循环检测此标志并重建）
		m_need_rebuild_octree = true;
	} else {
		spdlog::info("Voxel destruction: no voxels removed at ({},{},{})", center.x, center.y, center.z);
	}
}
