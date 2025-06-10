#include "VoxelDestroyer.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include "myvk/CommandBuffer.hpp"

std::shared_ptr<VoxelDestroyer> VoxelDestroyer::Create(
    const std::shared_ptr<Octree> &octree,
    const std::shared_ptr<Camera> &camera,
    const std::shared_ptr<myvk::Device> &device) {
    
    std::shared_ptr<VoxelDestroyer> ret = std::make_shared<VoxelDestroyer>();
    ret->m_octree_ptr = octree;
    ret->m_camera_ptr = camera;
    
    ret->create_buffers(device);
    ret->create_descriptors(device);
    ret->create_pipeline(device);
    
    return ret;
}

void VoxelDestroyer::create_buffers(const std::shared_ptr<myvk::Device> &device) {
    m_ray_buffer = myvk::Buffer::Create(device, sizeof(glm::vec4),
                                       VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void VoxelDestroyer::create_descriptors(const std::shared_ptr<myvk::Device> &device) {
    m_descriptor_pool = myvk::DescriptorPool::Create(device, 1, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}});
    
    VkDescriptorSetLayoutBinding ray_binding = {};
    ray_binding.binding = 0;
    ray_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ray_binding.descriptorCount = 1;
    ray_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    m_descriptor_set_layout = myvk::DescriptorSetLayout::Create(device, {ray_binding});
    m_descriptor_set = myvk::DescriptorSet::Create(m_descriptor_pool, m_descriptor_set_layout);
    
    m_descriptor_set->UpdateStorageBuffer(m_ray_buffer, 0);
}

void VoxelDestroyer::create_pipeline(const std::shared_ptr<myvk::Device> &device) {
    m_pipeline_layout = myvk::PipelineLayout::Create(device, {m_octree_ptr->GetDescriptorSetLayout(),
                                                             m_camera_ptr->GetDescriptorSetLayout(),
                                                             m_descriptor_set_layout}, {});
    
    constexpr uint32_t kVoxelDestroyCompSpv[] = {
#include "spirv/voxel_destroy.comp.u32"
    };
    std::shared_ptr<myvk::ShaderModule> voxel_destroy_shader_module =
        myvk::ShaderModule::Create(device, kVoxelDestroyCompSpv, sizeof(kVoxelDestroyCompSpv));
    
    VkSpecializationMapEntry specialization_entry = {};
    specialization_entry.constantID = 0;
    specialization_entry.offset = 0;
    specialization_entry.size = sizeof(uint32_t);
    
    uint32_t voxel_resolution = 1u << m_octree_ptr->GetLevel();
    VkSpecializationInfo specialization_info = {};
    specialization_info.mapEntryCount = 1;
    specialization_info.pMapEntries = &specialization_entry;
    specialization_info.dataSize = sizeof(uint32_t);
    specialization_info.pData = &voxel_resolution;
    
    m_destroy_pipeline = myvk::ComputePipeline::Create(m_pipeline_layout, voxel_destroy_shader_module, &specialization_info);
}



void VoxelDestroyer::HandleInput(GLFWwindow *window) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        double cursor_x, cursor_y;
        glfwGetCursorPos(window, &cursor_x, &cursor_y);
        
    }
}

void VoxelDestroyer::DestroyVoxelAtCursor(const std::shared_ptr<myvk::CommandBuffer> &command_buffer,
                                         float cursor_x, float cursor_y, uint32_t current_frame) {
    if (m_octree_ptr->Empty()) {
        spdlog::warn("VoxelDestroyer: Octree is empty, skipping destruction");
        return;
    }
    
    glm::vec2 normalized_coords = glm::vec2(cursor_x / float(m_screen_width), cursor_y / float(m_screen_height));
    
    spdlog::info("VoxelDestroyer: Destroying voxel at cursor ({}, {}) -> normalized ({}, {})", 
                 cursor_x, cursor_y, normalized_coords.x, normalized_coords.y);
    spdlog::info("VoxelDestroyer: Screen size: {}x{}, Octree level: {}", 
                 m_screen_width, m_screen_height, m_octree_ptr->GetLevel());
    
    struct RayData {
        glm::vec2 screen_coords;
        glm::vec2 padding;
    } ray_data = {normalized_coords, glm::vec2(0.0f)};
    
    m_ray_buffer->UpdateData(ray_data);
    
    command_buffer->CmdBindDescriptorSets({m_octree_ptr->GetDescriptorSet(),
                                          m_camera_ptr->GetFrameDescriptorSet(current_frame),
                                          m_descriptor_set},
                                         m_pipeline_layout, VK_PIPELINE_BIND_POINT_COMPUTE, {});
    command_buffer->CmdBindPipeline(m_destroy_pipeline);
    command_buffer->CmdDispatch(1, 1, 1);
    
    command_buffer->CmdPipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
        {}, {m_octree_ptr->GetBuffer()->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)}, {});
    
    spdlog::info("VoxelDestroyer: Compute shader dispatched for voxel destruction");
}
