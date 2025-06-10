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
    m_ray_buffer = myvk::Buffer::Create(device, 2 * sizeof(glm::vec3),
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

glm::vec3 VoxelDestroyer::screen_to_world_ray(float screen_x, float screen_y) const {
    glm::vec2 coord = glm::vec2(screen_x / float(m_screen_width), screen_y / float(m_screen_height));
    coord = coord * 2.0f - 1.0f;
    
    float yaw = m_camera_ptr->m_yaw;
    float pitch = m_camera_ptr->m_pitch;
    
    glm::vec3 look = glm::vec3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
    glm::vec3 side = glm::normalize(glm::cross(look, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(side, look));
    
    return glm::normalize(look - side * coord.x - up * coord.y);
}

void VoxelDestroyer::HandleInput(GLFWwindow *window) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        double cursor_x, cursor_y;
        glfwGetCursorPos(window, &cursor_x, &cursor_y);
        
    }
}

void VoxelDestroyer::DestroyVoxelAtCursor(const std::shared_ptr<myvk::CommandBuffer> &command_buffer,
                                         float cursor_x, float cursor_y) {
    if (m_octree_ptr->Empty()) {
        return;
    }
    
    glm::vec3 ray_origin = m_camera_ptr->m_position;
    glm::vec3 ray_direction = screen_to_world_ray(cursor_x, cursor_y);
    
    struct RayData {
        glm::vec3 origin;
        glm::vec3 direction;
    } ray_data = {ray_origin, ray_direction};
    
    m_ray_buffer->UpdateData(ray_data);
    
    command_buffer->CmdBindDescriptorSets({m_octree_ptr->GetDescriptorSet(),
                                          m_camera_ptr->GetFrameDescriptorSet(0),
                                          m_descriptor_set},
                                         m_pipeline_layout, VK_PIPELINE_BIND_POINT_COMPUTE, {});
    command_buffer->CmdBindPipeline(m_destroy_pipeline);
    command_buffer->CmdDispatch(1, 1, 1);
    
    command_buffer->CmdPipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, {},
        {m_octree_ptr->GetBuffer()->GetMemoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)}, {});
}
