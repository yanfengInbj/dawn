// Copyright 2018 The NXT Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "backend/vulkan/RenderPipelineVk.h"

#include "backend/vulkan/FencedDeleter.h"
#include "backend/vulkan/InputStateVk.h"
#include "backend/vulkan/PipelineLayoutVk.h"
#include "backend/vulkan/RenderPassVk.h"
#include "backend/vulkan/ShaderModuleVk.h"
#include "backend/vulkan/VulkanBackend.h"

namespace backend { namespace vulkan {

    namespace {

        VkPrimitiveTopology VulkanPrimitiveTopology(nxt::PrimitiveTopology topology) {
            switch (topology) {
                case nxt::PrimitiveTopology::PointList:
                    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                case nxt::PrimitiveTopology::LineList:
                    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                case nxt::PrimitiveTopology::LineStrip:
                    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case nxt::PrimitiveTopology::TriangleList:
                    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                case nxt::PrimitiveTopology::TriangleStrip:
                    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                default:
                    UNREACHABLE();
            }
        }

    }  // anonymous namespace

    RenderPipeline::RenderPipeline(RenderPipelineBuilder* builder)
        : RenderPipelineBase(builder), mDevice(ToBackend(builder->GetDevice())) {
        // Eventually a bunch of the structures that need to be chained in the create info will be
        // held by objects such as the BlendState. They aren't implemented yet so we initialize
        // everything here.

        VkPipelineShaderStageCreateInfo shaderStages[2];
        {
            const auto& vertexStageInfo = builder->GetStageInfo(nxt::ShaderStage::Vertex);
            const auto& fragmentStageInfo = builder->GetStageInfo(nxt::ShaderStage::Fragment);

            shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[0].pNext = nullptr;
            shaderStages[0].flags = 0;
            shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shaderStages[0].module = ToBackend(vertexStageInfo.module)->GetHandle();
            shaderStages[0].pName = vertexStageInfo.entryPoint.c_str();
            shaderStages[0].pSpecializationInfo = nullptr;

            shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStages[1].pNext = nullptr;
            shaderStages[1].flags = 0;
            shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shaderStages[1].module = ToBackend(fragmentStageInfo.module)->GetHandle();
            shaderStages[1].pName = fragmentStageInfo.entryPoint.c_str();
            shaderStages[1].pSpecializationInfo = nullptr;
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly;
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.pNext = nullptr;
        inputAssembly.flags = 0;
        inputAssembly.topology = VulkanPrimitiveTopology(GetPrimitiveTopology());
        // Primitive restart is always enabled in NXT (because of Metal)
        inputAssembly.primitiveRestartEnable = VK_TRUE;

        // A dummy viewport/scissor info. The validation layers force use to provide at least one
        // scissor and one viewport here, even if we choose to make them dynamic.
        VkViewport viewportDesc;
        viewportDesc.x = 0.0f;
        viewportDesc.y = 0.0f;
        viewportDesc.width = 1.0f;
        viewportDesc.height = 1.0f;
        viewportDesc.minDepth = 0.0f;
        viewportDesc.maxDepth = 1.0f;
        VkRect2D scissorRect;
        scissorRect.offset.x = 0;
        scissorRect.offset.y = 0;
        scissorRect.extent.width = 1;
        scissorRect.extent.height = 1;
        VkPipelineViewportStateCreateInfo viewport;
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.pNext = nullptr;
        viewport.flags = 0;
        viewport.viewportCount = 1;
        viewport.pViewports = &viewportDesc;
        viewport.scissorCount = 1;
        viewport.pScissors = &scissorRect;

        VkPipelineRasterizationStateCreateInfo rasterization;
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.pNext = nullptr;
        rasterization.flags = 0;
        rasterization.depthClampEnable = VK_FALSE;
        rasterization.rasterizerDiscardEnable = VK_FALSE;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.depthBiasEnable = VK_FALSE;
        rasterization.depthBiasConstantFactor = 0.0f;
        rasterization.depthBiasClamp = 0.0f;
        rasterization.depthBiasSlopeFactor = 0.0f;
        rasterization.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample;
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.pNext = nullptr;
        multisample.flags = 0;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisample.sampleShadingEnable = VK_FALSE;
        multisample.minSampleShading = 0.0f;
        multisample.pSampleMask = nullptr;
        multisample.alphaToCoverageEnable = VK_FALSE;
        multisample.alphaToOneEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil;
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.pNext = nullptr;
        depthStencil.flags = 0;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front.failOp = VK_STENCIL_OP_KEEP;
        depthStencil.front.passOp = VK_STENCIL_OP_KEEP;
        depthStencil.front.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencil.front.compareOp = VK_COMPARE_OP_NEVER;
        depthStencil.front.compareMask = 0;
        depthStencil.front.writeMask = 0;
        depthStencil.front.reference = 0;
        depthStencil.back.failOp = VK_STENCIL_OP_KEEP;
        depthStencil.back.passOp = VK_STENCIL_OP_KEEP;
        depthStencil.back.depthFailOp = VK_STENCIL_OP_KEEP;
        depthStencil.back.compareOp = VK_COMPARE_OP_NEVER;
        depthStencil.back.compareMask = 0;
        depthStencil.back.writeMask = 0;
        depthStencil.back.reference = 0;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 0.0f;

        // Even when not using independent blend, we need to provide blend information for every
        // single attachment.
        std::array<VkPipelineColorBlendAttachmentState, kMaxColorAttachments> colorBlendAttachments;
        for (auto& attachment : colorBlendAttachments) {
            attachment.blendEnable = VK_FALSE;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo colorBlend;
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.pNext = nullptr;
        colorBlend.flags = 0;
        colorBlend.logicOpEnable = VK_FALSE;
        colorBlend.logicOp = VK_LOGIC_OP_CLEAR;
        colorBlend.attachmentCount = kMaxColorAttachments;
        colorBlend.pAttachments = colorBlendAttachments.data();
        colorBlend.blendConstants[0] = 0.0f;
        colorBlend.blendConstants[1] = 0.0f;
        colorBlend.blendConstants[2] = 0.0f;
        colorBlend.blendConstants[3] = 0.0f;

        // Tag all state as dynamic.
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        VkPipelineDynamicStateCreateInfo dynamic;
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.pNext = nullptr;
        dynamic.flags = 0;
        dynamic.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
        dynamic.pDynamicStates = dynamicStates;

        // The create info chains in a bunch of things created on the stack here or inside state
        // objects.
        VkGraphicsPipelineCreateInfo createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.stageCount = 2;
        createInfo.pStages = shaderStages;
        createInfo.pVertexInputState = ToBackend(GetInputState())->GetCreateInfo();
        createInfo.pInputAssemblyState = &inputAssembly;
        createInfo.pTessellationState = nullptr;
        createInfo.pViewportState = &viewport;
        createInfo.pRasterizationState = &rasterization;
        createInfo.pMultisampleState = &multisample;
        createInfo.pDepthStencilState = &depthStencil;
        createInfo.pColorBlendState = &colorBlend;
        createInfo.pDynamicState = &dynamic;
        createInfo.layout = ToBackend(GetLayout())->GetHandle();
        createInfo.renderPass = ToBackend(GetRenderPass())->GetHandle();
        createInfo.subpass = GetSubPass();
        createInfo.basePipelineHandle = VK_NULL_HANDLE;
        createInfo.basePipelineIndex = -1;

        if (mDevice->fn.CreateGraphicsPipelines(mDevice->GetVkDevice(), VK_NULL_HANDLE, 1,
                                                &createInfo, nullptr, &mHandle) != VK_SUCCESS) {
            ASSERT(false);
        }
    }

    RenderPipeline::~RenderPipeline() {
        if (mHandle != VK_NULL_HANDLE) {
            mDevice->GetFencedDeleter()->DeleteWhenUnused(mHandle);
            mHandle = VK_NULL_HANDLE;
        }
    }

    VkPipeline RenderPipeline::GetHandle() const {
        return mHandle;
    }

}}  // namespace backend::vulkan