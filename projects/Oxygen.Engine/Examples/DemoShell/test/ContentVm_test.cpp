//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentModelCatalog.h"
#include "DemoShell/UI/ContentMountList.h"
#include "DemoShell/UI/ContentSceneCatalog.h"
#include "DemoShell/UI/ContentSourceFolders.h"
#include "DemoShell/UI/ContentVm.h"
#include "DemoShell/test/ContentFixtures.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::examples::testing {
namespace {

  using data::CookedSourceKind;

  //! Content settings held in memory, so tests share no settings file.
  class MemoryContentSettings final : public ContentSettingsService {
  public:
    std::vector<ContentSourceSetting> sources;
    std::vector<std::filesystem::path> records;
    std::optional<ContentSceneSetting> scene;
    std::vector<RecentSceneSetting> recent;
    ImportProfile profile = DefaultImportProfile();
    ContentImportTabSettings tab {};

    auto GetMountSources() const -> std::vector<ContentSourceSetting> override
    {
      return sources;
    }
    auto SetMountSources(const std::vector<ContentSourceSetting>& value)
      -> void override
    {
      sources = value;
    }
    auto GetMountedImportRecords() const
      -> std::vector<std::filesystem::path> override
    {
      return records;
    }
    auto SetMountedImportRecords(
      const std::vector<std::filesystem::path>& value) -> void override
    {
      records = value;
    }
    auto GetLoadedScene() const -> std::optional<ContentSceneSetting> override
    {
      return scene;
    }
    auto SetLoadedScene(const std::optional<ContentSceneSetting>& value)
      -> void override
    {
      scene = value;
    }
    auto GetRecentScenes() const -> std::vector<RecentSceneSetting> override
    {
      return recent;
    }
    auto SetRecentScenes(const std::vector<RecentSceneSetting>& value)
      -> void override
    {
      recent = value;
    }
    auto GetImportProfile() const -> ImportProfile override { return profile; }
    auto SetImportProfile(const ImportProfile& value) -> void override
    {
      profile = value;
    }
    auto GetImportTab() const -> ContentImportTabSettings override
    {
      return tab;
    }
    auto SetImportTab(const ContentImportTabSettings& value) -> void override
    {
      tab = value;
    }
  };

  //! A glTF file with one empty scene, the smallest model an import takes.
  auto WriteMinimalGltf(const std::filesystem::path& file) -> void
  {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file);
    output
      << R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0]}],"scene":0})";
  }

  //! Updates `vm` until its import finishes, for at most 15 seconds, and
  //! once more for its model states.
  auto WaitForImport(ui::ContentVm& vm) -> void
  {
    const auto deadline
      = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (
      vm.IsImportInProgress() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      vm.Update();
    }
    EXPECT_FALSE(vm.IsImportInProgress());
    // The finished import's model states refresh on the next update.
    vm.Update();
  }

  //! A view model on a real loader; its mount list is applied as a host
  //! applies it, with `runtime::ApplyContentMounts`.
  class ContentVmTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override
    {
      root_ = std::filesystem::temp_directory_path()
        / ("oxygen-content-vm-" + Uuid::Generate().ToString());
      std::filesystem::create_directories(root_ / "Content" / "SourceMedia");
      browser_.ConfigureContentRoots({
        .content_root = root_,
        .cooked_root = root_ / ".cooked",
        .pak_directory = root_,
        .scratch_root = root_,
      });
      auto config = content::AssetLoaderConfig {};
      config.thread_pool = observer_ptr { &pool_ };
      loader_.emplace(engine::internal::EngineTagFactory::Get(), config);
    }

    auto TearDown() -> void override
    {
      loader_.reset();
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      EXPECT_FALSE(error);
    }

    //! A view model whose mount requests are applied at once and recorded.
    auto MakeVm() -> std::unique_ptr<ui::ContentVm>
    {
      auto vm = std::make_unique<ui::ContentVm>(observer_ptr { &settings_ },
        observer_ptr { &browser_ }, observer_ptr { &Loader() });
      vm->SetOnMountsRequested(
        [this, vm = vm.get()](
          std::vector<content::ContentMount> mounts) -> void {
          requested_.push_back(mounts);
          vm->OnMountsApplied(co::Run(
            loop_, runtime::ApplyContentMounts(Loader(), std::move(mounts))));
        });
      vm->SetOnSceneLoadRequested([this](const ui::SceneEntry& scene) -> void {
        loads_.push_back(scene);
      });
      return vm;
    }

    [[nodiscard]] auto Root() const -> const std::filesystem::path&
    {
      return root_;
    }

    static auto Dir(const std::filesystem::path& path) -> ContentSourceSetting
    {
      return { .kind = CookedSourceKind::kLooseCooked, .path = path };
    }

    static auto Pak(const std::filesystem::path& path) -> ContentSourceSetting
    {
      return { .kind = CookedSourceKind::kPak, .path = path };
    }

    [[nodiscard]] auto Loader() -> content::AssetLoader&
    {
      if (!loader_) {
        throw std::logic_error("the loader is made in SetUp");
      }
      return *loader_;
    }

    MemoryContentSettings settings_ {};
    FileBrowserService browser_;
    std::vector<std::vector<content::ContentMount>> requested_;
    std::vector<ui::SceneEntry> loads_;

  private:
    std::filesystem::path root_;
    co::testing::TestEventLoop loop_;
    co::ThreadPool pool_ { loop_, 2 };
    std::optional<content::AssetLoader> loader_;
  };

  //! Added sources mount in precedence order: the last added is on top, and
  //! the set the host applies lists it last.
  NOLINT_TEST_F(ContentVmTest, AddedSourcesMountTopFirst)
  {
    // Arrange
    const auto lower = WriteShowcaseRoot(Root() / "lower");
    const auto upper = WriteShowcaseRoot(Root() / "upper", 2U);
    auto vm = MakeVm();

    // Act
    vm->AddSource(Dir(lower.root));
    vm->AddSource(Dir(upper.root));

    // Assert
    const auto& rows = vm->Mounts().Rows();
    ASSERT_GE(rows.size(), 2U);
    EXPECT_EQ(rows.at(0).path, upper.root);
    EXPECT_EQ(rows.at(1).path, lower.root);
    EXPECT_EQ(rows.at(0).state, ui::SourceState::kMounted);
    EXPECT_EQ(rows.at(1).state, ui::SourceState::kMounted);
    ASSERT_FALSE(requested_.empty());
    EXPECT_EQ(requested_.back().back().path, upper.root);
    EXPECT_EQ(
      settings_.sources, (std::vector { Dir(upper.root), Dir(lower.root) }));
  }

  //! Removing and moving a source reapplies the whole list.
  NOLINT_TEST_F(ContentVmTest, RemoveAndMoveReapplyTheList)
  {
    // Arrange
    const auto first = WriteShowcaseRoot(Root() / "first");
    const auto second = WriteShowcaseRoot(Root() / "second", 2U);
    auto vm = MakeVm();
    vm->AddSource(Dir(first.root));
    vm->AddSource(Dir(second.root));
    const auto applied = requested_.size();

    // Act
    vm->MoveSource({ .from = 1, .to = 0 });
    const auto moved = vm->Mounts().Added();
    vm->RemoveSource(0);

    // Assert
    EXPECT_EQ(moved, (std::vector { Dir(first.root), Dir(second.root) }));
    EXPECT_EQ(requested_.size(), applied + 2);
    EXPECT_EQ(settings_.sources, std::vector { Dir(second.root) });
    ASSERT_FALSE(requested_.back().empty());
    EXPECT_EQ(requested_.back().back().path, second.root);
  }

  //! A pak whose requirement fails keeps its row, refused with the failure,
  //! and logs the refusal; the other sources still mount.
  NOLINT_TEST_F(ContentVmTest, RefusedPakKeepsItsRowAndFailures)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    const auto paks = WriteDependentPak(Root() / "paks");
    auto vm = MakeVm();
    vm->AddSource(Dir(showcase.root));

    // Act
    vm->AddSource(Pak(paks.pak));

    // Assert
    const auto& rows = vm->Mounts().Rows();
    const auto pak_row = vm->Mounts().IndexOf(paks.pak);
    const auto root_row = vm->Mounts().IndexOf(showcase.root);
    ASSERT_HAS_VALUE(pak_row);
    ASSERT_HAS_VALUE(root_row);
    const auto& refused = rows.at(*pak_row);
    EXPECT_EQ(refused.state, ui::SourceState::kRefused);
    ASSERT_EQ(refused.failures.size(), 1U);
    EXPECT_EQ(
      refused.failures.front().failure, content::RequirementFailure::kMissing);
    EXPECT_EQ(refused.failures.front().requirement.key, paks.required);
    EXPECT_EQ(rows.at(*root_row).state, ui::SourceState::kMounted);
    EXPECT_TRUE(vm->Mounts().HasProblem());
    const auto& entries = vm->Log().Entries();
    const auto logged = std::ranges::find(
      entries, ui::log_codes::kMountRefused, &ui::ContentLogEntry::code);
    ASSERT_NE(logged, entries.end());
    EXPECT_EQ(logged->severity, ui::LogSeverity::kError);
    EXPECT_EQ(logged->cooked_source, paks.pak);
  }

  //! The refused pak mounts once what it requires is below it.
  NOLINT_TEST_F(ContentVmTest, RefusedPakMountsAboveItsRequirement)
  {
    // Arrange
    const auto paks = WriteDependentPak(Root() / "paks");
    auto vm = MakeVm();
    vm->AddSource(Pak(paks.pak));

    // Act
    vm->AddSource(Pak(paks.base_pak));
    vm->MoveSource({ .from = 0, .to = 1 });

    // Assert
    const auto row = vm->Mounts().IndexOf(paks.pak);
    ASSERT_HAS_VALUE(row);
    EXPECT_EQ(*row, 0U);
    EXPECT_EQ(vm->Mounts().Rows().at(*row).state, ui::SourceState::kMounted);
    EXPECT_FALSE(vm->Mounts().HasProblem());
  }

  //! A source that is gone is marked missing and left out of the set.
  NOLINT_TEST_F(ContentVmTest, MissingSourceIsLeftOut)
  {
    // Arrange
    auto vm = MakeVm();
    const auto gone = Root() / "gone";

    // Act
    vm->AddSource(Dir(gone));

    // Assert
    const auto row = vm->Mounts().IndexOf(gone);
    ASSERT_HAS_VALUE(row);
    EXPECT_EQ(vm->Mounts().Rows().at(*row).state, ui::SourceState::kMissing);
    ASSERT_FALSE(requested_.empty());
    EXPECT_TRUE(std::ranges::none_of(requested_.back(),
      [&gone](const auto& mount) -> bool { return mount.path == gone; }));
    EXPECT_TRUE(
      std::ranges::any_of(vm->Log().Entries(), [](const auto& entry) -> bool {
        return entry.code == ui::log_codes::kMountSourceMissing;
      }));
  }

  //! Rows are named by their summary's mount and count its assets.
  NOLINT_TEST_F(ContentVmTest, RowsCarryTheirSummaries)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    auto vm = MakeVm();

    // Act
    vm->AddSource(Dir(showcase.root));

    // Assert
    const auto row = vm->Mounts().IndexOf(showcase.root);
    ASSERT_HAS_VALUE(row);
    const auto& source = vm->Mounts().Rows().at(*row);
    EXPECT_EQ(ui::MountNameOf(source), "/Showcase");
    ASSERT_HAS_VALUE(source.summary);
    EXPECT_EQ(source.summary->assets.size(), 2U);
    auto error = std::string {};
    const auto* details = vm->SourceDetailsOf(*row, error);
    ASSERT_NE(details, nullptr) << error;
    EXPECT_EQ(details->assets.size(), 2U);
  }

  //! Scenes group by mount; a scene two sources hold comes from the top one.
  NOLINT_TEST_F(ContentVmTest, ScenesGroupByMountFromTheTopSource)
  {
    // Arrange
    const auto lower = WriteShowcaseRoot(Root() / "lower");
    const auto upper = WriteShowcaseRoot(Root() / "upper", 2U);
    auto vm = MakeVm();

    // Act
    vm->AddSource(Dir(lower.root));
    vm->AddSource(Dir(upper.root));

    // Assert
    const auto& groups = vm->Scenes().Groups();
    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups.front().mount, "/Showcase");
    EXPECT_EQ(groups.front().scenes.size(), 2U);
    const auto* bottle = vm->Scenes().Find(lower.bottle);
    ASSERT_NE(bottle, nullptr);
    EXPECT_EQ(bottle->name, "BottleOnBox");
    EXPECT_EQ(bottle->virtual_path, lower.bottle_path);
    EXPECT_EQ(std::filesystem::weakly_canonical(bottle->source_path),
      std::filesystem::weakly_canonical(upper.root));
  }

  //! A loaded scene is shown, saved and first among the recent scenes.
  NOLINT_TEST_F(ContentVmTest, LoadedSceneIsRememberedAndRestored)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    {
      auto vm = MakeVm();
      vm->AddSource(Dir(showcase.root));
      const auto* scene = vm->Scenes().Find(showcase.bottle);
      ASSERT_NE(scene, nullptr);

      // Act
      vm->RequestSceneLoad(*scene);
      vm->NotifySceneLoadCompleted(showcase.bottle, true);

      // Assert
      const auto& loaded = vm->GetLoadedScene();
      ASSERT_HAS_VALUE(loaded);
      EXPECT_EQ(loaded->scene.key, showcase.bottle);
      EXPECT_EQ(loaded->asset_count, 1U);
      ASSERT_HAS_VALUE(settings_.scene);
      EXPECT_EQ(settings_.scene->virtual_path, showcase.bottle_path);
      ASSERT_EQ(settings_.recent.size(), 1U);
      EXPECT_EQ(settings_.recent.front().virtual_path, showcase.bottle_path);
    }
    loads_.clear();
    auto restored = MakeVm();
    restored->RestorePersistedState();
    ASSERT_EQ(loads_.size(), 1U);
    EXPECT_EQ(loads_.front().key, showcase.bottle);
  }

  //! A failed load is logged with the host's reason and loads nothing.
  NOLINT_TEST_F(ContentVmTest, FailedSceneLoadIsLogged)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    auto vm = MakeVm();
    vm->AddSource(Dir(showcase.root));
    const auto* scene = vm->Scenes().Find(showcase.lantern);
    ASSERT_NE(scene, nullptr);
    vm->RequestSceneLoad(*scene);

    // Act
    vm->NotifySceneLoadCompleted(showcase.lantern, false, "texture missing");

    // Assert
    EXPECT_FALSE(vm->GetLoadedScene().has_value());
    EXPECT_FALSE(vm->IsSceneLoading());
    const auto& entry = vm->Log().Entries().back();
    EXPECT_EQ(entry.code, ui::log_codes::kSceneLoadFailed);
    EXPECT_EQ(entry.details, "texture missing");
    EXPECT_EQ(entry.scene, showcase.lantern_path);
    EXPECT_EQ(entry.cooked_source, scene->source_path);
  }

  //! The saved list mounts again, in its order, at the next launch.
  NOLINT_TEST_F(ContentVmTest, RestoresTheMountListFromItsKeys)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    const auto paks = WriteDependentPak(Root() / "paks");
    settings_.sources
      = { Pak(paks.pak), Pak(paks.base_pak), Dir(showcase.root) };
    auto vm = MakeVm();

    // Act
    vm->RestorePersistedState();
    vm->RestorePersistedState();

    // Assert
    ASSERT_EQ(requested_.size(), 1U);
    EXPECT_EQ(vm->Mounts().Added(), settings_.sources);
    for (const auto& source : settings_.sources) {
      const auto row = vm->Mounts().IndexOf(source.path);
      ASSERT_HAS_VALUE(row);
      EXPECT_EQ(vm->Mounts().Rows().at(*row).state, ui::SourceState::kMounted);
    }
  }

  //! A model imported with the panel's settings; its record and generation.
  class ContentVmModelTest : public ContentVmTest {
  protected:
    auto SetUp() -> void override
    {
      ContentVmTest::SetUp();
      model_ = Root() / "Content" / "SourceMedia" / "source.gltf";
      WriteMinimalGltf(model_);
      const auto mounts = ui::MakeScratchMounts(browser_.GetContentRoots());
      using content::import::ModelImportRecord;
      auto settings = ui::MakeModelImportSettings(DefaultImportProfile());
      settings.source_path = model_.string();
      settings.virtual_path
        = ui::ScratchModelVirtualPath("/Showcase/SourceMedia/source.gltf");
      auto findings = std::vector<content::import::ImportDiagnostic> {};
      const auto record = ModelImportRecord::RecordPathOf(
        mounts, settings.virtual_path, findings);
      ASSERT_HAS_VALUE(record);
      record_ = *record;
      ModelImportRecord::Save({
        .record_path = record_,
        .source = model_,
        .settings_json = ModelImportRecord::MakeSettings(settings),
        .mounts = make_observer(&mounts),
      });
      std::promise<content::import::ImportReport> completed;
      auto future = completed.get_future();
      content::import::AsyncImportService service(
        content::import::AsyncImportService::Config { .thread_pool_size = 2 });
      const auto stop
        = ScopeGuard([&service] noexcept -> void { service.Stop(); });
      ASSERT_TRUE(service.SubmitModelImport(
        ModelImportRecord::Prepare(record_,
          content::import::ModelImportGenerations {
            .root
            = Root() / ".cooked" / ModelImportRecord::kModelGenerationsFolder,
          },
          mounts),
        [&completed](
          auto, const auto& report) -> auto { completed.set_value(report); }));
      ASSERT_EQ(
        future.wait_for(std::chrono::seconds(15)), std::future_status::ready);
      const auto report = future.get();
      ASSERT_TRUE(report.success);
      generation_ = report.cooked_root;
      settings_.tab.source_mount = "Showcase";
    }

    //! Imports the model with `vm`, whose host takes the new generation to
    //! mount later; returns the generations it was handed.
    auto ImportWithHostMount(ui::ContentVm& vm)
      -> std::vector<runtime::GenerationPublication>
    {
      auto published = std::vector<runtime::GenerationPublication> {};
      vm.SetOnGenerationPublished(
        [&published](
          const runtime::GenerationPublication& publication) -> void {
          published.push_back(publication);
        });
      vm.StartImport(model_);
      WaitForImport(vm);
      return published;
    }

    std::filesystem::path model_;
    std::filesystem::path record_;
    std::filesystem::path generation_;
  };

  //! A model whose record holds the panel's settings is imported; changing a
  //! texture setting makes its settings differ in the Textures group.
  NOLINT_TEST_F(ContentVmModelTest, ModelStateFollowsTheSettingsDifference)
  {
    // Arrange
    auto vm = MakeVm();
    const auto* model = vm->Models().Find(model_);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->state, ui::ModelState::kImported);

    // Act
    auto profile = vm->GetImportProfile();
    profile.tuning.max_size = 512U;
    vm->SetImportProfile(profile);
    vm->Update();

    // Assert
    EXPECT_EQ(
      vm->Models().Find(model_)->state, ui::ModelState::kSettingsChanged);
    EXPECT_EQ(vm->ModelRecordOf(model_).differing,
      std::vector { ui::ImportSettingsGroup::kTextures });
    EXPECT_EQ(vm->ChangedImportGroups(),
      std::vector { ui::ImportSettingsGroup::kTextures });
    EXPECT_EQ(vm->ModelRecordOf(model_).generation, generation_);
  }

  //! A mounted record's selected generation is restored above the added
  //! sources.
  NOLINT_TEST_F(ContentVmModelTest, RestoresTheRecordsGeneration)
  {
    // Arrange
    const auto showcase = WriteShowcaseRoot(Root() / "Showcase");
    settings_.sources = { Dir(showcase.root) };
    settings_.records = { record_ };
    auto vm = MakeVm();

    // Act
    vm->RestorePersistedState();

    // Assert
    const auto& rows = vm->Mounts().Rows();
    ASSERT_GE(rows.size(), 2U);
    EXPECT_EQ(rows.front().origin, ui::SourceOrigin::kImportRecord);
    EXPECT_EQ(rows.front().path, generation_);
    EXPECT_EQ(rows.front().state, ui::SourceState::kMounted);
    EXPECT_EQ(vm->Mounts().Records(), std::vector { record_ });
    ASSERT_EQ(requested_.size(), 1U);
    EXPECT_EQ(requested_.front().back().path, generation_);
  }

  //! With load after import on, the imported scene loads once the host has
  //! mounted the new generation, not when the import finishes: until then
  //! the scene is not in any mounted source.
  NOLINT_TEST_F(ContentVmModelTest, ImportedSceneLoadsOnceItsGenerationMounts)
  {
    // Arrange
    settings_.tab.load_scene_after_import = true;
    auto vm = MakeVm();
    const auto published = ImportWithHostMount(*vm);
    ASSERT_EQ(published.size(), 1U);
    EXPECT_TRUE(loads_.empty());

    // Act
    runtime::MountGeneration(Loader(), published.front());
    vm->OnGenerationMounted(published.front(), std::nullopt);

    // Assert
    ASSERT_EQ(loads_.size(), 1U);
    EXPECT_EQ(loads_.front().source_path, published.front().cooked_root);
  }

  //! A generation the host could not mount loads no scene.
  NOLINT_TEST_F(ContentVmModelTest, UnmountedGenerationLoadsNoScene)
  {
    // Arrange
    settings_.tab.load_scene_after_import = true;
    auto vm = MakeVm();
    const auto published = ImportWithHostMount(*vm);
    ASSERT_EQ(published.size(), 1U);

    // Act
    vm->OnGenerationMounted(published.front(), "the writer holds it");

    // Assert
    EXPECT_TRUE(loads_.empty());
  }

  //! Listing the records again keeps the rows of generations already listed,
  //! mounted as they were; only a new generation waits for its mount.
  NOLINT_TEST(ContentMountListTest, RelistedGenerationKeepsItsState)
  {
    // Arrange
    auto mounts = ui::ContentMountList {};
    const auto first = ui::RecordGeneration {
      .record = "a.gltf.import.json",
      .generation = "generations/a/1",
    };
    mounts.SetRecords({ first });
    mounts.SetRowState(0, ui::SourceState::kMounted, {});

    // Act
    mounts.SetRecords({
      { .record = "b.gltf.import.json", .generation = "generations/b/1" },
      first,
    });

    // Assert
    const auto kept = mounts.IndexOf(first.generation);
    ASSERT_HAS_VALUE(kept);
    EXPECT_EQ(mounts.Rows().at(*kept).state, ui::SourceState::kMounted);
    const auto added = mounts.IndexOf("generations/b/1");
    ASSERT_HAS_VALUE(added);
    EXPECT_EQ(mounts.Rows().at(*added).state, ui::SourceState::kPending);
  }

  //! The Import tab lists the examples' content, never the scratch mount,
  //! which holds the imports' records.
  NOLINT_TEST_F(ContentVmTest, SourceFoldersLeaveOutTheScratchMount)
  {
    // Arrange
    auto vm = MakeVm();

    // Act
    const auto folders = vm->SourceFolders();

    // Assert
    ASSERT_EQ(folders.size(), 1U);
    EXPECT_EQ(folders.front().name, "Showcase");
    EXPECT_FALSE(folders.front().removable);
  }

  //! An added folder is a source folder of this machine, kept in the scratch
  //! project's `.mounts.json`, listed and removable; removing it forgets it.
  NOLINT_TEST_F(ContentVmTest, AddedSourceFolderIsListedAndRemovable)
  {
    // Arrange
    std::filesystem::create_directories(Root() / "My Models");
    auto vm = MakeVm();

    // Act
    const auto name = vm->AddSourceFolder(Root() / "My Models");

    // Assert
    ASSERT_HAS_VALUE(name);
    EXPECT_EQ(*name, "My_Models");
    const auto added = [&vm, &name] -> bool {
      return std::ranges::any_of(
        vm->SourceFolders(), [&name](const ui::SourceFolder& folder) -> bool {
          return folder.name == *name && folder.removable;
        });
    };
    EXPECT_TRUE(added());
    EXPECT_TRUE(std::filesystem::exists(ui::LocalMountsFile(Root())));
    vm->RemoveSourceFolder(*name);
    EXPECT_FALSE(added());
  }

  //! A model file in no source folder is imported from where it is: its
  //! folder becomes a source folder, and the tab lists that folder's models.
  NOLINT_TEST_F(ContentVmTest, ImportFileMountsItsFolder)
  {
    // Arrange
    const auto file = Root() / "Elsewhere" / "thing.gltf";
    WriteMinimalGltf(file);
    auto vm = MakeVm();

    // Act
    vm->ImportFile(file);
    WaitForImport(*vm);

    // Assert
    EXPECT_EQ(vm->GetImportTab().source_mount, "Elsewhere");
    const auto* model
      = vm->Models().Find(std::filesystem::weakly_canonical(file));
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->state, ui::ModelState::kImported);
  }

  //! A folder's name becomes a mount name: characters a mount name does not
  //! allow become `_`, a leading digit gets one, and a taken name a number.
  NOLINT_TEST(SourceFolderMountNameTest, MakesAFreeValidName)
  {
    const auto taken = std::vector<std::string> { "Downloads", "Content" };
    EXPECT_EQ(ui::SourceFolderMountName("D:/My Models", taken), "My_Models");
    EXPECT_EQ(ui::SourceFolderMountName("D:/3d", taken), "_3d");
    EXPECT_EQ(ui::SourceFolderMountName("D:/Downloads", taken), "Downloads_2");
    EXPECT_EQ(ui::SourceFolderMountName("D:/Engine", taken), "Engine_2");
  }

  //! Adding and removing a mount keeps what else the mounts file holds.
  NOLINT_TEST(LocalMountsTest, EditingKeepsTheFilesOtherContent)
  {
    // Arrange
    const auto root = std::filesystem::temp_directory_path()
      / ("oxygen-local-mounts-" + Uuid::Generate().ToString());
    const auto remove = ScopeGuard([&root] noexcept -> void {
      std::error_code error;
      std::filesystem::remove_all(root, error);
    });
    const auto file = ui::LocalMountsFile(root);
    std::filesystem::create_directories(root);
    {
      std::ofstream output(file);
      output << R"({"version":1,"root":true,"mounts":{"Kept":"D:/kept"}})";
    }

    // Act
    ui::AddLocalMount(file, "Added", "D:/added");
    ui::RemoveLocalMount(file, "Kept");

    // Assert
    std::ifstream input(file);
    const auto document = nlohmann::json::parse(input);
    EXPECT_EQ(document.at("version"), 1);
    EXPECT_EQ(document.at("root"), true);
    EXPECT_EQ(
      document.at("mounts"), nlohmann::json({ { "Added", "D:/added" } }));
  }

  //! A record that selects no generation is a missing row, kept for retry.
  NOLINT_TEST_F(ContentVmTest, RecordWithoutGenerationIsMissing)
  {
    // Arrange
    settings_.records = { Root() / "Scratch" / "missing.gltf.import.json" };
    auto vm = MakeVm();

    // Act
    vm->RestorePersistedState();

    // Assert
    ASSERT_FALSE(vm->Mounts().Rows().empty());
    EXPECT_EQ(vm->Mounts().Rows().front().state, ui::SourceState::kMissing);
    EXPECT_EQ(vm->Mounts().Records(), settings_.records);
  }

  //! An ad-hoc model is identified by its source's path below the examples'
  //! source media, in the scratch project's `Scratch` mount, so two files of
  //! one name in two folders are two models.
  NOLINT_TEST(ScratchModelVirtualPathTest, NamesTheSourceByItsMountPath)
  {
    EXPECT_EQ(
      ui::ScratchModelVirtualPath("/Showcase/SourceMedia/DCC/a/Watch.gltf"),
      "/Scratch/DCC/a/Watch.gltf");
    EXPECT_EQ(
      ui::ScratchModelVirtualPath("/Showcase/SourceMedia/DCC/b/Watch.gltf"),
      "/Scratch/DCC/b/Watch.gltf");
  }

  //! A source in a local mount keeps its mount's name, below the `Scratch`
  //! mount.
  NOLINT_TEST(ScratchModelVirtualPathTest, KeepsALocalMountsName)
  {
    EXPECT_EQ(ui::ScratchModelVirtualPath("/Downloads/Watch.glb"),
      "/Scratch/Downloads/Watch.glb");
  }

  //! Differences are named by group, whatever the order of the two objects.
  NOLINT_TEST(ImportProfileTest, DifferingGroupsNameTheChangedGroups)
  {
    const auto* const lhs
      = R"({"import_lights":true,"max_size":0,"up_axis":"y"})";
    const auto* const rhs = R"({"import_lights":false,"max_size":0})";
    const auto expected = std::vector {
      ui::ImportSettingsGroup::kGeometry,
      ui::ImportSettingsGroup::kLightsAndCameras,
    };
    EXPECT_EQ(ui::DifferingGroups(lhs, rhs), expected);
    EXPECT_EQ(ui::DifferingGroups(rhs, lhs), expected);
    EXPECT_TRUE(ui::DifferingGroups(lhs, lhs).empty());
  }

  //! The default profile changes no group; a reset group is back to it.
  NOLINT_TEST(ImportProfileTest, ResetGroupRestoresTheDefaults)
  {
    auto profile = DefaultImportProfile();
    EXPECT_TRUE(ui::ChangedGroups(profile).empty());
    profile.options.import_lights = false;
    profile.importer.thread_pool_size = 3;
    EXPECT_EQ(ui::ChangedGroups(profile),
      (std::vector { ui::ImportSettingsGroup::kLightsAndCameras,
        ui::ImportSettingsGroup::kImporter }));
    profile = ui::ResetGroup(profile, ui::ImportSettingsGroup::kImporter);
    EXPECT_EQ(ui::ChangedGroups(profile),
      std::vector { ui::ImportSettingsGroup::kLightsAndCameras });
  }

} // namespace
} // namespace oxygen::examples::testing
