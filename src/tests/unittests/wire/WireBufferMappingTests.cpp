// Copyright 2019 The Dawn Authors
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

#include "tests/unittests/wire/WireTest.h"

using namespace testing;
using namespace dawn_wire;

namespace {

    // Mock classes to add expectations on the wire calling callbacks
    class MockBufferMapReadCallback {
      public:
        MOCK_METHOD3(Call,
                     void(dawnBufferMapAsyncStatus status,
                          const uint32_t* ptr,
                          dawnCallbackUserdata userdata));
    };

    std::unique_ptr<MockBufferMapReadCallback> mockBufferMapReadCallback;
    void ToMockBufferMapReadCallback(dawnBufferMapAsyncStatus status,
                                     const void* ptr,
                                     dawnCallbackUserdata userdata) {
        // Assume the data is uint32_t to make writing matchers easier
        mockBufferMapReadCallback->Call(status, static_cast<const uint32_t*>(ptr), userdata);
    }

    class MockBufferMapWriteCallback {
      public:
        MOCK_METHOD3(Call,
                     void(dawnBufferMapAsyncStatus status,
                          uint32_t* ptr,
                          dawnCallbackUserdata userdata));
    };

    std::unique_ptr<MockBufferMapWriteCallback> mockBufferMapWriteCallback;
    uint32_t* lastMapWritePointer = nullptr;
    void ToMockBufferMapWriteCallback(dawnBufferMapAsyncStatus status,
                                      void* ptr,
                                      dawnCallbackUserdata userdata) {
        // Assume the data is uint32_t to make writing matchers easier
        lastMapWritePointer = static_cast<uint32_t*>(ptr);
        mockBufferMapWriteCallback->Call(status, lastMapWritePointer, userdata);
    }

}  // anonymous namespace

class WireBufferMappingTests : public WireTest {
  public:
    WireBufferMappingTests() : WireTest(true) {
    }
    ~WireBufferMappingTests() override = default;

    void SetUp() override {
        WireTest::SetUp();

        mockBufferMapReadCallback = std::make_unique<MockBufferMapReadCallback>();
        mockBufferMapWriteCallback = std::make_unique<MockBufferMapWriteCallback>();

        {
            dawnBufferDescriptor descriptor;
            descriptor.nextInChain = nullptr;

            apiBuffer = api.GetNewBuffer();
            buffer = dawnDeviceCreateBuffer(device, &descriptor);

            EXPECT_CALL(api, DeviceCreateBuffer(apiDevice, _))
                .WillOnce(Return(apiBuffer))
                .RetiresOnSaturation();
            EXPECT_CALL(api, BufferRelease(apiBuffer));
            FlushClient();
        }
        {
            dawnBufferDescriptor descriptor;
            descriptor.nextInChain = nullptr;

            errorBuffer = dawnDeviceCreateBuffer(device, &descriptor);

            EXPECT_CALL(api, DeviceCreateBuffer(apiDevice, _))
                .WillOnce(Return(nullptr))
                .RetiresOnSaturation();
            FlushClient();
        }
    }

    void TearDown() override {
        WireTest::TearDown();

        // Delete mocks so that expectations are checked
        mockBufferMapReadCallback = nullptr;
        mockBufferMapWriteCallback = nullptr;
    }

  protected:
    // A successfully created buffer
    dawnBuffer buffer;
    dawnBuffer apiBuffer;

    // An buffer that wasn't created on the server side
    dawnBuffer errorBuffer;
};

// MapRead-specific tests

// Check mapping for reading a succesfully created buffer
TEST_F(WireBufferMappingTests, MappingForReadSuccessBuffer) {
    dawnCallbackUserdata userdata = 8653;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                    &bufferContent);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(bufferContent)), userdata))
        .Times(1);

    FlushServer();

    dawnBufferUnmap(buffer);
    EXPECT_CALL(api, BufferUnmap(apiBuffer)).Times(1);

    FlushClient();
}

// Check that things work correctly when a validation error happens when mapping the buffer for
// reading
TEST_F(WireBufferMappingTests, ErrorWhileMappingForRead) {
    dawnCallbackUserdata userdata = 8654;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();
}

// Check mapping for reading a buffer that didn't get created on the server side
TEST_F(WireBufferMappingTests, MappingForReadErrorBuffer) {
    dawnCallbackUserdata userdata = 8655;
    dawnBufferMapReadAsync(errorBuffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback,
                           userdata);

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();

    dawnBufferUnmap(errorBuffer);

    FlushClient();
}

// Check that the map read callback is called with UNKNOWN when the buffer is destroyed before the
// request is finished
TEST_F(WireBufferMappingTests, DestroyBeforeReadRequestEnd) {
    dawnCallbackUserdata userdata = 8656;
    dawnBufferMapReadAsync(errorBuffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback,
                           userdata);

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_UNKNOWN, nullptr, userdata))
        .Times(1);

    dawnBufferRelease(errorBuffer);
}

// Check the map read callback is called with UNKNOWN when the map request would have worked, but
// Unmap was called
TEST_F(WireBufferMappingTests, UnmapCalledTooEarlyForRead) {
    dawnCallbackUserdata userdata = 8657;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                    &bufferContent);
        }));

    FlushClient();

    // Oh no! We are calling Unmap too early!
    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_UNKNOWN, nullptr, userdata))
        .Times(1);
    dawnBufferUnmap(buffer);

    // The callback shouldn't get called, even when the request succeeded on the server side
    FlushServer();
}

// Check that an error map read callback gets nullptr while a buffer is already mapped
TEST_F(WireBufferMappingTests, MappingForReadingErrorWhileAlreadyMappedGetsNullptr) {
    // Successful map
    dawnCallbackUserdata userdata = 34098;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                    &bufferContent);
        }))
        .RetiresOnSaturation();

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(bufferContent)), userdata))
        .Times(1);

    FlushServer();

    // Map failure while the buffer is already mapped
    userdata++;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();
}

// Test that the MapReadCallback isn't fired twice when unmap() is called inside the callback
TEST_F(WireBufferMappingTests, UnmapInsideMapReadCallback) {
    dawnCallbackUserdata userdata = 2039;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                    &bufferContent);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(bufferContent)), userdata))
        .WillOnce(InvokeWithoutArgs([&]() { dawnBufferUnmap(buffer); }));

    FlushServer();

    EXPECT_CALL(api, BufferUnmap(apiBuffer)).Times(1);

    FlushClient();
}

// Test that the MapReadCallback isn't fired twice the buffer external refcount reaches 0 in the
// callback
TEST_F(WireBufferMappingTests, DestroyInsideMapReadCallback) {
    dawnCallbackUserdata userdata = 2039;
    dawnBufferMapReadAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapReadCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapReadAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapReadCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                    &bufferContent);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapReadCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(bufferContent)), userdata))
        .WillOnce(InvokeWithoutArgs([&]() { dawnBufferRelease(buffer); }));

    FlushServer();

    FlushClient();
}

// MapWrite-specific tests

// Check mapping for writing a succesfully created buffer
TEST_F(WireBufferMappingTests, MappingForWriteSuccessBuffer) {
    dawnCallbackUserdata userdata = 8653;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    uint32_t serverBufferContent = 31337;
    uint32_t updatedContent = 4242;
    uint32_t zero = 0;

    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                     &serverBufferContent);
        }));

    FlushClient();

    // The map write callback always gets a buffer full of zeroes.
    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(zero)), userdata))
        .Times(1);

    FlushServer();

    // Write something to the mapped pointer
    *lastMapWritePointer = updatedContent;

    dawnBufferUnmap(buffer);
    EXPECT_CALL(api, BufferUnmap(apiBuffer)).Times(1);

    FlushClient();

    // After the buffer is unmapped, the content of the buffer is updated on the server
    ASSERT_EQ(serverBufferContent, updatedContent);
}

// Check that things work correctly when a validation error happens when mapping the buffer for
// writing
TEST_F(WireBufferMappingTests, ErrorWhileMappingForWrite) {
    dawnCallbackUserdata userdata = 8654;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();
}

// Check mapping for writing a buffer that didn't get created on the server side
TEST_F(WireBufferMappingTests, MappingForWriteErrorBuffer) {
    dawnCallbackUserdata userdata = 8655;
    dawnBufferMapWriteAsync(errorBuffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback,
                            userdata);

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();

    dawnBufferUnmap(errorBuffer);

    FlushClient();
}

// Check that the map write callback is called with UNKNOWN when the buffer is destroyed before the
// request is finished
TEST_F(WireBufferMappingTests, DestroyBeforeWriteRequestEnd) {
    dawnCallbackUserdata userdata = 8656;
    dawnBufferMapWriteAsync(errorBuffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback,
                            userdata);

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_UNKNOWN, nullptr, userdata))
        .Times(1);

    dawnBufferRelease(errorBuffer);
}

// Check the map read callback is called with UNKNOWN when the map request would have worked, but
// Unmap was called
TEST_F(WireBufferMappingTests, UnmapCalledTooEarlyForWrite) {
    dawnCallbackUserdata userdata = 8657;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    uint32_t bufferContent = 31337;
    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                     &bufferContent);
        }));

    FlushClient();

    // Oh no! We are calling Unmap too early!
    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_UNKNOWN, nullptr, userdata))
        .Times(1);
    dawnBufferUnmap(buffer);

    // The callback shouldn't get called, even when the request succeeded on the server side
    FlushServer();
}

// Check that an error map read callback gets nullptr while a buffer is already mapped
TEST_F(WireBufferMappingTests, MappingForWritingErrorWhileAlreadyMappedGetsNullptr) {
    // Successful map
    dawnCallbackUserdata userdata = 34098;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    uint32_t bufferContent = 31337;
    uint32_t zero = 0;
    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                     &bufferContent);
        }))
        .RetiresOnSaturation();

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(zero)), userdata))
        .Times(1);

    FlushServer();

    // Map failure while the buffer is already mapped
    userdata++;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);
    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_ERROR, nullptr, userdata))
        .Times(1);

    FlushServer();
}

// Test that the MapWriteCallback isn't fired twice when unmap() is called inside the callback
TEST_F(WireBufferMappingTests, UnmapInsideMapWriteCallback) {
    dawnCallbackUserdata userdata = 2039;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    uint32_t bufferContent = 31337;
    uint32_t zero = 0;
    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                     &bufferContent);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(zero)), userdata))
        .WillOnce(InvokeWithoutArgs([&]() { dawnBufferUnmap(buffer); }));

    FlushServer();

    EXPECT_CALL(api, BufferUnmap(apiBuffer)).Times(1);

    FlushClient();
}

// Test that the MapWriteCallback isn't fired twice the buffer external refcount reaches 0 in the
// callback
TEST_F(WireBufferMappingTests, DestroyInsideMapWriteCallback) {
    dawnCallbackUserdata userdata = 2039;
    dawnBufferMapWriteAsync(buffer, 40, sizeof(uint32_t), ToMockBufferMapWriteCallback, userdata);

    uint32_t bufferContent = 31337;
    uint32_t zero = 0;
    EXPECT_CALL(api, OnBufferMapWriteAsyncCallback(apiBuffer, 40, sizeof(uint32_t), _, _))
        .WillOnce(InvokeWithoutArgs([&]() {
            api.CallMapWriteCallback(apiBuffer, DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS,
                                     &bufferContent);
        }));

    FlushClient();

    EXPECT_CALL(*mockBufferMapWriteCallback,
                Call(DAWN_BUFFER_MAP_ASYNC_STATUS_SUCCESS, Pointee(Eq(zero)), userdata))
        .WillOnce(InvokeWithoutArgs([&]() { dawnBufferRelease(buffer); }));

    FlushServer();

    FlushClient();
}
