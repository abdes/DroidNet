//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>

#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Loader/Detail/PlatformServices.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

using oxygen::SerializedBackendConfig;
using oxygen::graphics::GraphicsModuleApi;
using oxygen::graphics::kGetGraphicsModuleApi;
using oxygen::loader::detail::PlatformServices;

using testing::NiceMock;

namespace {

// Mock Graphics class for testing
class MockGraphics : public oxygen::Graphics {
public:
  explicit MockGraphics(const SerializedBackendConfig& config)
    : Graphics("MockGraphics")
  {
    // Make a copy of the json_data since it's const char* and might be freed
    if (config.json_data != nullptr && config.size > 0) {
      json_copy = std::string(config.json_data, config.size);
    }
  }

  // clang-format off
  // NOLINTBEGIN(modernize-use-trailing-return-type)
  // MOCK_METHOD(std::unique_ptr<oxygen::imgui::ImguiModule>, CreateImGuiModule, (oxygen::EngineWeakPtr, oxygen::platform::WindowIdType), (const, override));
  MOCK_METHOD((const oxygen::graphics::DescriptorAllocator&), GetDescriptorAllocator, (), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::IShaderByteCode>, GetShader, (std::string_view), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::Surface>, CreateSurface, (std::weak_ptr<oxygen::platform::Window>, oxygen::observer_ptr<oxygen::graphics::CommandQueue>), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::CommandQueue>, CreateCommandQueue, (const oxygen::graphics::QueueKey&, oxygen::graphics::QueueRole), (override));
  MOCK_METHOD(std::unique_ptr<oxygen::graphics::CommandList>, CreateCommandListImpl, (oxygen::graphics::QueueRole, std::string_view), (override));
  MOCK_METHOD(std::unique_ptr<oxygen::graphics::CommandRecorder>, CreateCommandRecorder, (std::shared_ptr<oxygen::graphics::CommandList>, oxygen::observer_ptr<oxygen::graphics::CommandQueue>), (override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::Texture>, CreateTexture, (const oxygen::graphics::TextureDesc&), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::Texture>, CreateTextureFromNativeObject, (const oxygen::graphics::TextureDesc&, const oxygen::graphics::NativeObject&), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::Buffer>, CreateBuffer, (const oxygen::graphics::BufferDesc&), (const, override));
  // NOLINTEND(modernize-use-trailing-return-type)
  // clang-format on

  // Getter for the JSON data
  [[nodiscard]] auto GetJsonData() const -> const std::string&
  {
    return json_copy;
  }

private:
  // Keep a persistent copy of the JSON data
  std::string json_copy;
};

// Use proper sized constants for pointer values
constexpr uintptr_t kModuleHandleValue = 0x1234567890ABCDEF;

class MockPlatformServices : public PlatformServices {
public:
  // NOLINTBEGIN(modernize-use-trailing-return-type)
  MOCK_METHOD(std::string, GetExecutableDirectory, (), (const, override));
  MOCK_METHOD(ModuleHandle, LoadModule, (const std::string&), (override));
  MOCK_METHOD(ModuleHandle, OpenMainExecutableModule, (), (override));
  MOCK_METHOD(void, CloseModule, (ModuleHandle), (override));
  MOCK_METHOD(bool, IsMainExecutableModule, (ModuleHandle), (const, override));
  MOCK_METHOD(
    ModuleHandle, GetModuleHandleFromReturnAddress, (void*), (const, override));

  // Make the protected method public in the mock for testing
  using PlatformServices::GetRawFunctionAddress;
  MOCK_METHOD(RawFunctionPtr, GetRawFunctionAddress,
    (ModuleHandle, const std::string&), (override));
  // NOLINTEND(modernize-use-trailing-return-type)
};

// Helper to convert from function pointer type to
// PlatformServices::RawFunctionPtr
template <typename Func>
auto FuncToRawPtr(Func* func) -> PlatformServices::RawFunctionPtr
{
  union {
    Func* specific;
    PlatformServices::RawFunctionPtr raw;
  } converter {};
  converter.specific = func;
  return converter.raw;
}

class GraphicsBackendLoaderTest : public testing::Test {
protected:
  // Mock backend implementation encapsulated in the test fixture
  class MockBackend {
  public:
    MockBackend()
      : mock_api { .CreateBackend = &MockBackend::CreateBackendStatic,
        .DestroyBackend = &MockBackend::DestroyBackendStatic }
    {
    }

    ~MockBackend()
    {
      // Clean up any remaining mock graphics
      DestroyBackendStatic();
    }

    OXYGEN_MAKE_NON_COPYABLE(MockBackend)
    OXYGEN_MAKE_NON_MOVABLE(MockBackend)

    // Instance accessor
    [[nodiscard]] static auto GetInstance() -> auto&
    {
      static MockBackend instance;
      return instance;
    }

    // Static function to get the mock API - needed for function pointers
    [[nodiscard]] static auto GetGraphicsModuleApiStatic()
    {
      return &GetInstance().mock_api;
    }

    // Getter for the current mock graphics object
    [[nodiscard]] static auto GetMockGraphics()
    {
      return GetInstance().mock_graphics.get();
    }

    // Raw function pointer for GetGraphicsModuleApi
    [[nodiscard]] static auto GetApiFunction()
    {
      return FuncToRawPtr(&MockBackend::GetGraphicsModuleApiStatic);
    }

  private:
    // Static callback for CreateBackend
    static auto CreateBackendStatic(const SerializedBackendConfig& config)
      -> void*
    {
      auto& instance = GetInstance();
      if (instance.mock_graphics == nullptr) {
        instance.mock_graphics = std::make_unique<MockGraphics>(config);
      }
      return instance.mock_graphics.get();
    }

    // Static callback for DestroyBackend
    static auto DestroyBackendStatic() -> void
    {
      auto& instance = GetInstance();
      instance.mock_graphics.reset();
    }

    GraphicsModuleApi mock_api;
    std::unique_ptr<MockGraphics> mock_graphics;
  };

  auto SetUp() -> void override
  {
    // Create a new mock backend for each test
    mock_backend = std::make_unique<MockBackend>();

    // Create the mock platform services
    platform = std::make_shared<NiceMock<MockPlatformServices>>();

    // Set up default behavior for the mock
    ON_CALL(*platform, GetExecutableDirectory())
      .WillByDefault(testing::Return("C:\\FakePath\\"));

    ON_CALL(*platform, LoadModule(testing::_))
      // NOLINTNEXTLINE
      .WillByDefault(testing::Return(reinterpret_cast<void*>(
        kModuleHandleValue))); // NOLINT(performance-no-int-to-ptr)

    // Use the encapsulated mock backend
    ON_CALL(*platform, GetRawFunctionAddress(testing::_, kGetGraphicsModuleApi))
      .WillByDefault(testing::Return(MockBackend::GetApiFunction()));

    ON_CALL(*platform, IsMainExecutableModule(testing::_))
      .WillByDefault(testing::Return(true));

    ON_CALL(*platform, GetModuleHandleFromReturnAddress(testing::_))
      // NOLINTNEXTLINE
      .WillByDefault(testing::Return(reinterpret_cast<void*>(
        kModuleHandleValue))); // NOLINT(performance-no-int-to-ptr)
  }

  auto TearDown() -> void override
  {
    platform.reset();
    MockBackend::GetInstance().~MockBackend();
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  std::shared_ptr<NiceMock<MockPlatformServices>> platform;
  std::unique_ptr<MockBackend> mock_backend;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

// Test successful initialization from main module
NOLINT_TEST_F(GraphicsBackendLoaderTest, GetInstanceFromMainModule)
{
  // This should succeed
  EXPECT_NO_THROW({
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);
    EXPECT_NE(&loader, nullptr);
  });
}

// Test initialization failure from non-main module (death test)
NOLINT_TEST_F(GraphicsBackendLoaderTest, GetInstanceFromNonMainModule)
{
  EXPECT_CALL(*platform, IsMainExecutableModule(testing::_))
    .WillOnce(testing::Return(false));

  // This should trigger a fatal error
  EXPECT_THROW(
    { oxygen::GraphicsBackendLoader::GetInstance(platform); },
    oxygen::loader::InvalidOperationError);
}

// Test successful backend loading from main module
NOLINT_TEST_F(GraphicsBackendLoaderTest, LoadBackendFromMainModule)
{
  // Get the loader instance
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // This should succeed
  oxygen::GraphicsConfig config;
  auto backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(backend.expired());
}
// Test unloading a backend
NOLINT_TEST_F(GraphicsBackendLoaderTest, UnloadBackendFromMainModule)
{
  // Get the loader instance and load a backend
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load a backend first
  oxygen::GraphicsConfig config;
  auto backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(backend.expired());

  // Expect CloseModule to be called exactly once
  EXPECT_CALL(*platform, CloseModule(testing::_)).Times(1);

  // This should succeed
  loader.UnloadBackend();

  // Backend should now be expired
  EXPECT_TRUE(backend.expired());
}

// Test that GetBackend returns a valid backend after loading
NOLINT_TEST_F(GraphicsBackendLoaderTest, GetBackendAfterLoading)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load a backend
  oxygen::GraphicsConfig config;
  auto loaded_backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);

  // GetBackend should return the same backend
  auto retrieved_backend = loader.GetBackend();
  EXPECT_FALSE(retrieved_backend.expired());

  // Both should point to the same underlying object
  EXPECT_EQ(loaded_backend.lock().get(), retrieved_backend.lock().get());
}

// Test that LoadBackend returns the same instance when called twice
NOLINT_TEST_F(GraphicsBackendLoaderTest, LoadBackendTwiceReturnsSameInstance)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load a backend twice
  oxygen::GraphicsConfig config;
  auto first_backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  auto second_backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);

  // Should be the same instance
  EXPECT_EQ(first_backend.lock().get(), second_backend.lock().get());
}

// Test loading a backend with different configurations
NOLINT_TEST_F(GraphicsBackendLoaderTest, LoadBackendWithDifferentConfigs)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load with debug configuration
  oxygen::GraphicsConfig debug_config;
  debug_config.enable_debug = true;
  auto backend = loader.LoadBackend(
    oxygen::graphics::BackendType::kDirect3D12, debug_config);
  EXPECT_FALSE(backend.expired());

  // Clean up by unloading
  EXPECT_CALL(*platform, CloseModule(testing::_)).Times(1);
  loader.UnloadBackend();

  // Setup expectations for second load
  testing::Mock::VerifyAndClearExpectations(platform.get());

  // Load with validation configuration
  oxygen::GraphicsConfig validation_config;
  validation_config.enable_validation = true;
  backend = loader.LoadBackend(
    oxygen::graphics::BackendType::kDirect3D12, validation_config);
  EXPECT_FALSE(backend.expired());
}

// Test error handling during backend loading
NOLINT_TEST_F(GraphicsBackendLoaderTest, LoadBackendErrorHandling)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Make LoadModule throw an exception
  EXPECT_CALL(*platform, LoadModule(testing::_))
    .WillOnce(testing::Throw(std::runtime_error("Module loading failed")));

  oxygen::GraphicsConfig config;
  EXPECT_THROW(auto _
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config),
    std::runtime_error);
}

// Test loading a backend after previous backend was unloaded
NOLINT_TEST_F(GraphicsBackendLoaderTest, LoadBackendAfterUnload)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load a backend
  oxygen::GraphicsConfig config;
  auto first_backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(first_backend.expired());

  // Unload it
  EXPECT_CALL(*platform, CloseModule(testing::_)).Times(1);
  loader.UnloadBackend();
  EXPECT_TRUE(first_backend.expired());
  testing::Mock::VerifyAndClearExpectations(platform.get());

  // Load another backend
  auto second_backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(second_backend.expired());

  // Should be a different instance than the first
  EXPECT_NE(first_backend.lock().get(), second_backend.lock().get());
}

// Test singleton reset with new platform services
NOLINT_TEST_F(
  GraphicsBackendLoaderTest, GetInstanceWithNewPlatformServices_ResetsTheLoader)
{
  auto& loader1 = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Load a backend to verify state transition
  oxygen::GraphicsConfig config;
  auto backend
    = loader1.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);

  // Reset the loader
  auto& loader2 = oxygen::GraphicsBackendLoader::GetInstance(platform);
  EXPECT_TRUE(backend.expired());

  // We get a new loader instance
  EXPECT_NE(&loader1, &loader2);

  // Should be able to load a new backend using the new platform services
  auto new_backend
    = loader2.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(new_backend.expired());
}

NOLINT_TEST_F(GraphicsBackendLoaderTest, ConfigSerialization)
{
  auto& loader = oxygen::GraphicsBackendLoader::GetInstance(platform);

  // Create a detailed config with various settings
  oxygen::GraphicsConfig config;
  config.enable_debug = true;
  config.enable_validation = true;
  config.headless = false;
  config.enable_imgui = true;
  config.preferred_card_name = "Test GPU";
  config.preferred_card_device_id = 1;
  config.extra = R"({"custom_key": "custom_value", "another_key": 42})";

  // Load backend with our config
  auto backend
    = loader.LoadBackend(oxygen::graphics::BackendType::kDirect3D12, config);
  EXPECT_FALSE(backend.expired());

  // Get access to the MockGraphics to verify the serialized config
  auto* mock_graphics = MockBackend::GetMockGraphics();
  ASSERT_NE(mock_graphics, nullptr);

  // Cast to MockGraphics to access our captured config
  auto* captured_mock = mock_graphics;

  // Convert the captured JSON to a string for inspection
  std::string json_str(captured_mock->GetJsonData());

  // Check that the config contains our values
  EXPECT_TRUE(
    json_str.find(R"("backend_type": "Direct3D12")") != std::string::npos);
  EXPECT_TRUE(json_str.find(R"("enable_debug": true)") != std::string::npos);
  EXPECT_TRUE(
    json_str.find(R"("enable_validation": true)") != std::string::npos);
  EXPECT_TRUE(json_str.find(R"("headless": false)") != std::string::npos);
  EXPECT_TRUE(json_str.find(R"("enable_imgui": true)") != std::string::npos);
  EXPECT_TRUE(
    json_str.find(R"("preferred_card_name": "Test GPU")") != std::string::npos);
  EXPECT_TRUE(
    json_str.find(R"("preferred_card_device_id": 1)") != std::string::npos);
  EXPECT_TRUE(
    json_str.find(R"("custom_key": "custom_value")") != std::string::npos);
  EXPECT_TRUE(json_str.find(R"("another_key": 42)") != std::string::npos);
}

} // namespace
