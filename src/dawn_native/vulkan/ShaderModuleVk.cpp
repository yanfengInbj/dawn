// Copyright 2018 The Dawn Authors
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

#include "dawn_native/vulkan/ShaderModuleVk.h"

#include "dawn_native/vulkan/DeviceVk.h"
#include "dawn_native/vulkan/FencedDeleter.h"
#include "dawn_native/vulkan/VulkanError.h"

#include <spirv_cross.hpp>

namespace dawn_native { namespace vulkan {

    // static
    ResultOrError<ShaderModule*> ShaderModule::Create(Device* device,
                                                      const ShaderModuleDescriptor* descriptor) {
        std::unique_ptr<ShaderModule> module = std::make_unique<ShaderModule>(device, descriptor);
        DAWN_TRY(module->Initialize(descriptor));
        return module.release();
    }

    MaybeError ShaderModule::Initialize(const ShaderModuleDescriptor* descriptor) {
        // Use SPIRV-Cross to extract info from the SPIRV even if Vulkan consumes SPIRV. We want to
        // have a translation step eventually anyway.
        spirv_cross::Compiler compiler(descriptor->code, descriptor->codeSize);
        ExtractSpirvInfo(compiler);

        VkShaderModuleCreateInfo createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;
        createInfo.codeSize = descriptor->codeSize * sizeof(uint32_t);
        createInfo.pCode = descriptor->code;

        Device* device = ToBackend(GetDevice());
        return CheckVkSuccess(
            device->fn.CreateShaderModule(device->GetVkDevice(), &createInfo, nullptr, &mHandle),
            "CreateShaderModule");
    }

    ShaderModule::~ShaderModule() {
        Device* device = ToBackend(GetDevice());

        if (mHandle != VK_NULL_HANDLE) {
            device->GetFencedDeleter()->DeleteWhenUnused(mHandle);
            mHandle = VK_NULL_HANDLE;
        }
    }

    VkShaderModule ShaderModule::GetHandle() const {
        return mHandle;
    }

}}  // namespace dawn_native::vulkan
