//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentVm.h"

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::examples::testing {
namespace {

  class MemoryContentSettings final : public ContentSettingsService {
  public:
    ContentExplorerSettings explorer {};
    std::vector<std::filesystem::path> paks {};
    std::vector<std::filesystem::path> indices {};
    std::vector<std::filesystem::path> records {};
    std::optional<ContentActiveSceneSelection> active_scene {};

    auto GetExplorerSettings() const -> ContentExplorerSettings override
    {
      return explorer;
    }
    auto GetMountedPakPaths() const
      -> std::vector<std::filesystem::path> override
    {
      return paks;
    }
    auto SetMountedPakPaths(const std::vector<std::filesystem::path>& paths)
      -> void override
    {
      paks = paths;
    }
    auto GetMountedIndexPaths() const
      -> std::vector<std::filesystem::path> override
    {
      return indices;
    }
    auto SetMountedIndexPaths(const std::vector<std::filesystem::path>& paths)
      -> void override
    {
      indices = paths;
    }
    auto GetMountedImportRecords() const
      -> std::vector<std::filesystem::path> override
    {
      return records;
    }
    auto SetMountedImportRecords(
      const std::vector<std::filesystem::path>& paths) -> void override
    {
      records = paths;
    }
    auto GetActiveSceneSelection() const
      -> std::optional<ContentActiveSceneSelection> override
    {
      return active_scene;
    }
    auto SetActiveSceneSelection(
      const std::optional<ContentActiveSceneSelection>& selection)
      -> void override
    {
      active_scene = selection;
    }
  };

  class ContentVmTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override
    {
      root_ = std::filesystem::temp_directory_path()
        / ("oxygen-content-vm-" + Uuid::Generate().ToString());
      std::filesystem::create_directory(root_);
      settings_.explorer.model_root = root_;
      browser_.ConfigureContentRoots(
        { .content_root = root_, .cooked_root = root_ / ".cooked" });
      const auto source = root_ / "source.gltf";
      {
        std::ofstream output(source);
        output
          << R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0]}],"scene":0})";
      }
      content::import::SceneImportSettings recipe {};
      recipe.source_path = source.string();
      record_ = root_ / "imports" / "source.import.json";
      using content::import::RetainedModelImport;
      RetainedModelImport::SaveRecipe(
        record_, root_, RetainedModelImport::MakeRecipe(recipe, {}));
      std::promise<content::import::ImportReport> completed;
      auto future = completed.get_future();
      content::import::AsyncImportService service(
        content::import::AsyncImportService::Config { .thread_pool_size = 2 });
      const auto stop
        = ScopeGuard([&service] noexcept -> void { service.Stop(); });
      ASSERT_TRUE(
        service.SubmitRetainedImport(RetainedModelImport::Prepare(record_),
          [&completed](auto, const auto& report) -> auto {
            completed.set_value(report);
          }));
      ASSERT_EQ(
        future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
      const auto report = future.get();
      ASSERT_TRUE(report.success);
      generation_ = report.cooked_root;
    }

    auto TearDown() -> void override
    {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      EXPECT_FALSE(error);
    }

    std::filesystem::path root_ {};
    std::filesystem::path record_ {};
    std::filesystem::path generation_ {};
    MemoryContentSettings settings_ {};
    FileBrowserService browser_ {};
  };

  NOLINT_TEST_F(
    ContentVmTest, OpeningRecordPersistsIdentityAndRestoresSelection)
  {
    std::vector<std::filesystem::path> mounted;
    {
      ui::ContentVm vm(
        observer_ptr { &settings_ }, observer_ptr { &browser_ }, nullptr);
      vm.SetOnGenerationPublished([&](const auto& publication) -> auto {
        mounted.push_back(publication.cooked_root);
      });
      vm.LoadImportRecord(record_);
      vm.LoadImportRecord(record_);
      EXPECT_EQ(settings_.records, std::vector { record_ });
      EXPECT_TRUE(settings_.indices.empty());
    }
    ASSERT_EQ(mounted.size(), 2U);
    EXPECT_EQ(mounted.front(), generation_);
    mounted.clear();
    settings_.paks = { root_ / "library.pak" };
    settings_.indices = { root_ / "shared" / "container.index.bin" };
    const auto expected_indices = settings_.indices;
    std::vector<std::filesystem::path> indices;
    ui::ContentVm restored(
      observer_ptr { &settings_ }, observer_ptr { &browser_ }, nullptr);
    restored.SetOnPakMounted([&](const auto&) -> auto {
      // A synchronous host can persist partial mount state during restoration.
      settings_.indices.clear();
      settings_.records.clear();
    });
    restored.SetOnIndexLoaded(
      [&](const auto& path) -> auto { indices.push_back(path); });
    restored.SetOnGenerationPublished([&](const auto& publication) -> auto {
      mounted.push_back(publication.cooked_root);
    });
    restored.RestorePersistedLibraryState();
    restored.RestorePersistedLibraryState();
    EXPECT_EQ(indices, expected_indices);
    EXPECT_EQ(mounted, std::vector { generation_ });
    EXPECT_EQ(settings_.records, std::vector { record_ });
  }

  NOLINT_TEST_F(ContentVmTest, MissingRecordIsNotMountedOrPersisted)
  {
    ui::ContentVm vm(
      observer_ptr { &settings_ }, observer_ptr { &browser_ }, nullptr);
    vm.SetOnGenerationPublished([](const auto&) -> auto {
      ADD_FAILURE() << "An unavailable import must not mount a generation";
    });
    vm.LoadImportRecord(root_ / "missing.import.json");
    EXPECT_TRUE(settings_.records.empty());
  }

  NOLINT_TEST_F(ContentVmTest, GenerationIndexRequiresItsAuthoredRecord)
  {
    ui::ContentVm vm(
      observer_ptr { &settings_ }, observer_ptr { &browser_ }, nullptr);
    vm.SetOnIndexLoaded([](const auto&) -> auto {
      ADD_FAILURE() << "A retained generation must use its authored record";
    });
    vm.LoadIndex(generation_ / "container.index.bin");
    EXPECT_TRUE(settings_.records.empty());
    EXPECT_TRUE(settings_.indices.empty());
    ASSERT_FALSE(vm.GetDiagnostics().empty());
    EXPECT_NE(vm.GetDiagnostics().back().message.find(".import.json"),
      std::string::npos);
  }

  NOLINT_TEST_F(ContentVmTest, FailedRetainedMountPreservesRecordForRetry)
  {
    settings_.records = { record_ };
    ui::ContentVm vm(
      observer_ptr { &settings_ }, observer_ptr { &browser_ }, nullptr);
    vm.SetOnGenerationPublished([](const auto&) -> void {
      throw std::runtime_error("Cooked generation temporarily unavailable");
    });
    vm.RestorePersistedLibraryState();
    vm.PrunePersistedMountedSource(
      ui::SceneSourceKind::kLooseIndex, generation_ / "container.index.bin");
    vm.PersistLibraryState();
    EXPECT_EQ(settings_.records, std::vector { record_ });

    std::vector<std::filesystem::path> mounted;
    vm.SetOnGenerationPublished([&](const auto& publication) -> void {
      mounted.push_back(publication.cooked_root);
    });
    vm.LoadImportRecord(record_);
    EXPECT_EQ(mounted, std::vector { generation_ });
    EXPECT_EQ(settings_.records, std::vector { record_ });
  }

  NOLINT_TEST_F(ContentVmTest, RestoresMatchingKeyDespiteChangedDisplayName)
  {
    content::AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(generation_);
    const auto scenes = loader.EnumerateMountedScenes();
    ASSERT_EQ(scenes.size(), 1U);
    const auto& scene = scenes.front();
    settings_.active_scene = ContentActiveSceneSelection {
      .scene_name = "Previous display label",
      .scene_key = nostd::to_string(scene.scene_key),
      .source_path = generation_ / "container.index.bin",
      .source_is_pak = false,
      .import_record_path = {},
    };
    ui::ContentVm vm(observer_ptr { &settings_ }, observer_ptr { &browser_ },
      observer_ptr { &loader });
    auto requested = std::vector<ui::SceneEntry> {};
    vm.SetOnSceneLoadRequested(
      [&](const auto& entry) { requested.push_back(entry); });
    vm.RestorePersistedLibraryState();
    ASSERT_EQ(requested.size(), 1U);
    EXPECT_EQ(requested.front().key, scene.scene_key);
    EXPECT_EQ(requested.front().name, scene.virtual_path);
  }

  NOLINT_TEST_F(ContentVmTest, NameOnlySelectionDoesNotLoadAnotherScene)
  {
    content::AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(generation_);
    settings_.active_scene = ContentActiveSceneSelection {
      .scene_name = "Missing scene",
      .scene_key = {},
      .source_path = generation_ / "container.index.bin",
      .source_is_pak = false,
      .import_record_path = {},
    };
    ui::ContentVm vm(observer_ptr { &settings_ }, observer_ptr { &browser_ },
      observer_ptr { &loader });
    vm.SetOnSceneLoadRequested([](const auto&) {
      ADD_FAILURE() << "The requested name does not exist";
    });
    vm.RestorePersistedLibraryState();
    EXPECT_FALSE(vm.IsSceneLoading());
  }

  NOLINT_TEST_F(ContentVmTest, RestoresNameOnlySelectionWhenNameMatches)
  {
    content::AssetLoader loader(engine::internal::EngineTagFactory::Get());
    loader.AddLooseCookedRoot(generation_);
    const auto scenes = loader.EnumerateMountedScenes();
    ASSERT_EQ(scenes.size(), 1U);
    const auto& scene = scenes.front();
    settings_.active_scene = ContentActiveSceneSelection {
      .scene_name = scene.virtual_path,
      .scene_key = {},
      .source_path = generation_ / "container.index.bin",
      .source_is_pak = false,
      .import_record_path = {},
    };
    ui::ContentVm vm(observer_ptr { &settings_ }, observer_ptr { &browser_ },
      observer_ptr { &loader });
    auto requested = std::vector<ui::SceneEntry> {};
    vm.SetOnSceneLoadRequested(
      [&](const auto& entry) { requested.push_back(entry); });
    vm.RestorePersistedLibraryState();
    ASSERT_EQ(requested.size(), 1U);
    EXPECT_EQ(requested.front().key, scene.scene_key);
  }
} // namespace
} // namespace oxygen::examples::testing
