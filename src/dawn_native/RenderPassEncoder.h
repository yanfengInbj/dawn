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

#ifndef DAWNNATIVE_RENDERPASSENCODER_H_
#define DAWNNATIVE_RENDERPASSENCODER_H_

#include "dawn_native/Error.h"
#include "dawn_native/RenderEncoderBase.h"

namespace dawn_native {

    class RenderBundleBase;

    // This is called RenderPassEncoderBase to match the code generator expectations. Note that it
    // is a pure frontend type to record in its parent CommandEncoder and never has a backend
    // implementation.
    // TODO(cwallez@chromium.org): Remove that generator limitation and rename to RenderPassEncoder
    class RenderPassEncoderBase : public RenderEncoderBase {
      public:
        RenderPassEncoderBase(DeviceBase* device,
                              CommandEncoderBase* commandEncoder,
                              EncodingContext* encodingContext);

        static RenderPassEncoderBase* MakeError(DeviceBase* device,
                                                CommandEncoderBase* commandEncoder,
                                                EncodingContext* encodingContext);

        void EndPass();

        void SetStencilReference(uint32_t reference);
        void SetBlendColor(const Color* color);
        void SetViewport(float x,
                         float y,
                         float width,
                         float height,
                         float minDepth,
                         float maxDepth);
        void SetScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void ExecuteBundles(uint32_t count, RenderBundleBase* const* renderBundles);

      protected:
        RenderPassEncoderBase(DeviceBase* device,
                              CommandEncoderBase* commandEncoder,
                              EncodingContext* encodingContext,
                              ErrorTag errorTag);

      private:
        // For render and compute passes, the encoding context is borrowed from the command encoder.
        // Keep a reference to the encoder to make sure the context isn't freed.
        Ref<CommandEncoderBase> mCommandEncoder;
    };

}  // namespace dawn_native

#endif  // DAWNNATIVE_RENDERPASSENCODER_H_
