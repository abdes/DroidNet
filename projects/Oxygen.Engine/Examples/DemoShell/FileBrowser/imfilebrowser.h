//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause AND MIT
//===----------------------------------------------------------------------===//
//
// Derived from imgui-filebrowser by Zhuang Guan (AirGuanZ):
//   https://github.com/AirGuanZ/imgui-filebrowser
// Copyright (c) 2019-2025 Zhuang Guan, distributed under the MIT License. The
// upstream license text is kept in LICENSE.txt next to this file and the
// imported revision in VERSION.txt. Oxygen's modifications are distributed
// under the BSD-3-Clause License stated above.
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/StringUtils.h>

namespace ImGui {

//! Behavior switches of the file browser window.
enum class FileBrowserFlags : std::uint32_t {
  kNone = 0,

  //! Select a directory instead of a regular file.
  kSelectDirectory = OXYGEN_FLAG(0),

  //! Let the user type a new filename when selecting a regular file.
  kEnterNewFilename = OXYGEN_FLAG(1),

  //! Use a plain popup instead of the default modal window.
  kNoModal = OXYGEN_FLAG(2),

  //! Hide the window title bar.
  kNoTitleBar = OXYGEN_FLAG(3),

  //! Hide the status text at the bottom of the window.
  kNoStatusBar = OXYGEN_FLAG(4),

  //! Close the browser when ESC is pressed.
  kCloseOnEsc = OXYGEN_FLAG(5),

  //! Let the user create new directories.
  kCreateNewDir = OXYGEN_FLAG(6),

  //! Let the user select several entries. Hides `kEnterNewFilename`.
  kMultipleSelection = OXYGEN_FLAG(7),

  //! Hide regular files when `kSelectDirectory` is enabled.
  kHideRegularFiles = OXYGEN_FLAG(8),

  //! Confirm the selection when ENTER is pressed.
  kConfirmOnEnter = OXYGEN_FLAG(9),

  //! Skip the entries that cause an error instead of falling back to the
  //! default directory when a directory cannot be listed.
  kSkipItemsCausingError = OXYGEN_FLAG(10),

  //! Let the user edit the whole path string.
  kEditPathString = OXYGEN_FLAG(11),
};

OXYGEN_DEFINE_FLAGS_OPERATORS(FileBrowserFlags)

//! Pure helpers behind `FileBrowser`, exposed for testing.
namespace file_browser_detail {

  //! Initial capacity of the text buffers edited through ImGui.
  inline constexpr std::size_t kInitialNameBufferSize = 32;

  //! Combines ImGui flags with unsigned arithmetic.
  template <typename... Flags>
  [[nodiscard]] constexpr auto CombineFlags(const Flags... flags) -> int
  {
    return static_cast<int>((static_cast<unsigned int>(flags) | ...));
  }

  //! Tells whether an ImGui flag set contains a flag.
  [[nodiscard]] constexpr auto HasImGuiFlag(const int flags, const int flag)
    -> bool
  {
    return (static_cast<unsigned int>(flags) & static_cast<unsigned int>(flag))
      != 0U;
  }

  //! Lowercases ASCII letters; every other byte is preserved.
  [[nodiscard]] inline auto ToLowerAscii(const std::string_view text)
    -> std::string
  {
    std::string lowered(text);
    for (char& c : lowered) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c + ('a' - 'A'));
      }
    }
    return lowered;
  }

  //! Converts a path to UTF-8 text.
  [[nodiscard]] inline auto PathToUtf8(const std::filesystem::path& path)
    -> std::string
  {
    std::string utf8;
    oxygen::string_utils::WideToUtf8(path.u8string(), utf8);
    return utf8;
  }

  //! Converts UTF-8 text to a path.
  [[nodiscard]] inline auto PathFromUtf8(const std::string_view text)
    -> std::filesystem::path
  {
    std::u8string utf8;
    oxygen::string_utils::WideToUtf8(text, utf8);
    return std::filesystem::path { utf8 };
  }

  //! Builds the key ordering directory listings.
  /*!
   Directories come first. Names compare case-insensitively with the lowercase
   spelling first, so `[b0, a0, A1]` sorts as `[a0, A1, b0]` rather than the
   plain lexicographic `[a0, b0, A1]`. Bytes outside ASCII keep their unsigned
   order, which is the code point order for UTF-8 text.
  */
  [[nodiscard]] inline auto MakeSortKey(const bool is_directory,
    const std::string_view name) -> std::vector<std::uint32_t>
  {
    std::vector<std::uint32_t> key;
    key.reserve(name.size() + 1);
    key.push_back(is_directory ? 0U : 1U);
    for (const char c : name) {
      const auto byte = static_cast<unsigned char>(c);
      if (byte >= 'A' && byte <= 'Z') {
        key.push_back(
          (2U * static_cast<std::uint32_t>(byte + ('a' - 'A'))) + 1U);
      } else {
        key.push_back(2U * static_cast<std::uint32_t>(byte));
      }
    }
    return key;
  }

  //! Tells whether a file extension passes the selected type filter.
  /*!
   @param filters Filter entries. When `has_all_filter` is true, entry 0 is the
          generated filter matching every other entry.
   @param has_all_filter Whether entry 0 is the generated "all" filter.
   @param index Selected filter. An out-of-range index matches everything.
   @param extension Extension of the candidate file, with the leading dot,
          already lowercased on case-insensitive platforms.
  */
  [[nodiscard]] inline auto MatchesTypeFilter(
    const std::vector<std::string>& filters, const bool has_all_filter,
    const std::size_t index, const std::string_view extension) -> bool
  {
    if (filters.empty() || index >= filters.size()) {
      return true;
    }
    if (has_all_filter && index == 0) {
      return std::ranges::any_of(std::next(filters.begin()), filters.end(),
        [extension](
          const std::string& filter) -> bool { return filter == extension; });
    }
    const std::string& filter = filters.at(index);
    return filter == ".*" || filter == extension;
  }

} // namespace file_browser_detail

//! A file and directory browser rendered with Dear ImGui.
/*!
 Call `Open()` to request the window and `Display()` every frame to draw it.
 After the user confirms, `HasSelected()` is true and `GetSelected()` or
 `GetMultiSelected()` return the choice. Call `ClearSelected()` to consume it.

 Copies are independent browsers with the same configuration and state; each
 browser owns a distinct window name (see `GetWindowName()`).
*/
class FileBrowser {
public:
  //! Creates a browser.
  /*!
   @param flags Behavior switches. `kSelectDirectory` and `kEnterNewFilename`
          are mutually exclusive.
   @param default_directory Initial directory, and the directory to fall back
          to when another one cannot be entered.
  */
  explicit FileBrowser(FileBrowserFlags flags = FileBrowserFlags::kNone,
    std::filesystem::path default_directory = std::filesystem::current_path());

  ~FileBrowser() = default;

  OXYGEN_DEFAULT_COPYABLE(FileBrowser)
  OXYGEN_DEFAULT_MOVABLE(FileBrowser)

  //! Sets the window position in pixels. Centered when never set.
  void SetWindowPos(ImVec2 position) noexcept;

  //! Sets the window size in pixels. Defaults to (700, 450).
  void SetWindowSize(ImVec2 size) noexcept;

  //! Sets the window title.
  void SetTitle(std::string title);

  //! Returns the unique ImGui window name, `<title>##filebrowser_<id>`.
  [[nodiscard]] auto GetWindowName() const -> std::string;

  //! Opens the window, refreshes the listing and clears the selection.
  void Open();

  //! Closes the window and clears the selection.
  void Close();

  //! Tells whether the window was displayed during the last `Display()`.
  [[nodiscard]] auto IsOpened() const noexcept -> bool;

  //! Draws the window when it is open. Call it every frame.
  void Display();

  //! Tells whether the user confirmed a selection.
  [[nodiscard]] auto HasSelected() const noexcept -> bool;

  //! Enters a directory.
  /*!
   When the directory cannot be entered, the browser keeps the current one
   (or falls back to the default directory) and reports the error in the
   status text.

   @return True when the requested directory was entered.
  */
  auto SetDirectory(const std::filesystem::path& directory
    = std::filesystem::current_path()) -> bool;

  //! Returns the directory being browsed.
  [[nodiscard]] auto GetDirectory() const noexcept
    -> const std::filesystem::path&;

  //! Returns the selected path. Meaningful when `HasSelected()` is true.
  /*!
   With `kMultipleSelection`, only one of the selected paths is returned. When
   nothing is selected, as happens when confirming in directory mode, returns
   the browsed directory.
  */
  [[nodiscard]] auto GetSelected() const -> std::filesystem::path;

  //! Returns every selected path; use it with `kMultipleSelection`.
  /*!
   When nothing is selected, returns the browsed directory alone.
  */
  [[nodiscard]] auto GetMultiSelected() const
    -> std::vector<std::filesystem::path>;

  //! Clears the selection and the confirmation.
  void ClearSelected();

  //! Sets the file type filters, e.g. `{ ".h", ".cpp", ".hpp" }`.
  /*!
   `".*"` matches any file. Several filters get an extra leading entry that
   matches all of them. Duplicates are dropped, and filters are compared
   case-insensitively on Windows.
  */
  void SetTypeFilters(const std::vector<std::string>& type_filters);

  //! Selects the applied type filter; the first one is index 0.
  /*!
   An index past the last filter selects the last one.
  */
  void SetCurrentTypeFilterIndex(std::size_t index);

  //! Pre-fills the filename input when `kEnterNewFilename` is set.
  void SetInputName(std::string_view input);

private:
  //! Distinguishes browser instances in ImGui IDs.
  /*!
   The value derives from the tag's own address, so a copy or a move creates a
   new value instead of inheriting the source's one.
  */
  class InstanceTag {
  public:
    InstanceTag()
      : value_(std::to_string(std::bit_cast<std::uintptr_t>(this)))
    {
    }

    InstanceTag(const InstanceTag& /*other*/)
      : InstanceTag()
    {
    }

    InstanceTag(InstanceTag&& /*other*/) noexcept
      : InstanceTag()
    {
    }

    auto operator=(const InstanceTag& /*other*/) -> InstanceTag&
    {
      return *this;
    }

    auto operator=(InstanceTag&& /*other*/) noexcept -> InstanceTag&
    {
      return *this;
    }

    ~InstanceTag() = default;

    [[nodiscard]] auto Value() const -> const std::string& { return value_; }

  private:
    std::string value_;
  };

  struct FileRecord {
    bool is_directory { false };
    std::filesystem::path name;
    std::string utf8_name;
    std::string display_name;
    std::string extension;
    std::vector<std::uint32_t> sort_key;
  };

  //! State of one `Display()` call shared by its drawing steps.
  struct DisplayState {
    std::optional<std::filesystem::path> pending_directory;
    bool focus_on_input_text { false };
  };

  [[nodiscard]] auto HasFlag(FileBrowserFlags flag) const noexcept -> bool
  {
    return (flags_ & flag) != FileBrowserFlags::kNone;
  }

  static void ToolTip(const char* text);

  static auto ExpandInputBuffer(ImGuiInputTextCallbackData* callback_data)
    -> int;

  static void SetBufferText(std::vector<char>& buffer, std::string_view text);

  static auto BufferText(const std::vector<char>& buffer) -> std::string_view;

  static auto NormalizeDirectory(const std::filesystem::path& directory)
    -> std::filesystem::path;

  [[nodiscard]] auto IsExtensionMatched(std::string_view extension) const
    -> bool;

  [[nodiscard]] auto IsSelectable(const FileRecord& record) const -> bool;

  [[nodiscard]] auto IsEntryVisible(const FileRecord& record) const -> bool;

  void UpdateFileRecords();

  void RefreshFileRecords();

  void ApplyDirectory(const std::filesystem::path& directory);

  auto TryApplyDirectory(const std::filesystem::path& directory)
    -> std::optional<std::string>;

  void ClearRangeSelectionState();

  void SetInputText(std::string_view text);

  void ApplyWindowGeometry() const;

  [[nodiscard]] auto BeginWindow(const std::string& window_name) const -> bool;

  void DrawPathEditor();

  void DrawPathBar();

  void DrawBreadcrumbs();

  void BeginPathEdit();

  void DrawToolbar(DisplayState& state);

  void DrawRefreshButton();

  void DrawNewDirectoryButton(DisplayState& state);

  void CreateDirectoryFromInput();

  void DrawEntryList(DisplayState& state);

  void DrawEntry(std::size_t index, DisplayState& state);

  void HandleEntryClicked(std::size_t index);

  void SelectRange(std::size_t first, std::size_t last);

  void ActivateEntry(const FileRecord& record, DisplayState& state);

  void DrawFileNameInput(DisplayState& state);

  void HandleSelectAll(const DisplayState& state);

  void DrawActionButtons();

  void DrawStatusText() const;

  void DrawTypeFilterCombo();

#ifdef OXYGEN_WINDOWS
  static auto GetDrivesBitMask() -> std::uint32_t;

  void DrawDriveSelector();
#endif

  FileBrowserFlags flags_;
  std::filesystem::path default_directory_;
  InstanceTag instance_tag_;

  ImVec2 window_size_ { 700.0F, 450.0F };
  std::optional<ImVec2> window_position_;
  std::string title_;

  bool should_open_ { false };
  bool should_close_ { false };
  bool is_opened_ { false };
  bool is_ok_ { false };

  std::string status_text_;

  std::vector<std::string> type_filters_;
  std::size_t type_filter_index_ { 0 };
  bool has_all_filter_ { false };

  std::filesystem::path current_directory_;
  std::vector<FileRecord> file_records_;

  // Anchor of the Shift range selection; unset when no entry can anchor it.
  std::optional<std::size_t> range_selection_start_;
  std::set<std::filesystem::path> selected_filenames_;

  std::vector<char> new_directory_name_buffer_;
  std::vector<char> input_name_buffer_;
  std::string customized_input_name_;

  bool edit_directory_ { false };
  bool focus_edit_directory_ { false };
  std::vector<char> current_directory_buffer_;

#ifdef OXYGEN_WINDOWS
  std::uint32_t drives_ { 0 };
#endif
};

} // namespace ImGui

inline ImGui::FileBrowser::FileBrowser(
  const FileBrowserFlags flags, std::filesystem::path default_directory)
  : flags_(flags)
  , default_directory_(std::move(default_directory))
  , title_("file browser")
{
  DCHECK_F(!(HasFlag(FileBrowserFlags::kSelectDirectory)
             && HasFlag(FileBrowserFlags::kEnterNewFilename)),
    "'kEnterNewFilename' doesn't work when 'kSelectDirectory' is enabled");
  if (HasFlag(FileBrowserFlags::kCreateNewDir)) {
    new_directory_name_buffer_.resize(
      file_browser_detail::kInitialNameBufferSize, '\0');
  }

  SetDirectory(default_directory_);

#ifdef OXYGEN_WINDOWS
  drives_ = GetDrivesBitMask();
#endif
}

inline void ImGui::FileBrowser::SetWindowPos(const ImVec2 position) noexcept
{
  window_position_ = position;
}

inline void ImGui::FileBrowser::SetWindowSize(const ImVec2 size) noexcept
{
  DCHECK_F(size.x > 0.0F && size.y > 0.0F, "window size must be positive");
  window_size_ = size;
}

inline void ImGui::FileBrowser::SetTitle(std::string title)
{
  title_ = std::move(title);
}

inline auto ImGui::FileBrowser::GetWindowName() const -> std::string
{
  return title_ + "##filebrowser_" + instance_tag_.Value();
}

inline void ImGui::FileBrowser::Open()
{
  RefreshFileRecords();
  ClearSelected();
  status_text_.clear();
  edit_directory_ = false;
  should_open_ = true;
  should_close_ = false;
  if (HasFlag(FileBrowserFlags::kEnterNewFilename)
    && !customized_input_name_.empty()) {
    SetBufferText(input_name_buffer_, customized_input_name_);
    selected_filenames_
      = { file_browser_detail::PathFromUtf8(customized_input_name_) };
  }
}

inline void ImGui::FileBrowser::Close()
{
  ClearSelected();
  status_text_.clear();
  should_close_ = true;
  should_open_ = false;
}

inline auto ImGui::FileBrowser::IsOpened() const noexcept -> bool
{
  return is_opened_;
}

inline void ImGui::FileBrowser::ApplyWindowGeometry() const
{
  // A freshly opened non-modal popup follows the requested geometry; every
  // other window keeps what the user did with it.
  const ImGuiCond condition
    = (should_open_ && HasFlag(FileBrowserFlags::kNoModal))
    ? ImGuiCond_Always
    : ImGuiCond_FirstUseEver;
  if (window_position_) {
    SetNextWindowPos(*window_position_, condition);
  }
  SetNextWindowSize(window_size_, condition);
}

inline auto ImGui::FileBrowser::BeginWindow(
  const std::string& window_name) const -> bool
{
  if (HasFlag(FileBrowserFlags::kNoModal)) {
    return BeginPopup(window_name.c_str());
  }
  return BeginPopupModal(window_name.c_str(), nullptr,
    HasFlag(FileBrowserFlags::kNoTitleBar) ? ImGuiWindowFlags_NoTitleBar
                                           : ImGuiWindowFlags_None);
}

inline void ImGui::FileBrowser::Display()
{
  PushID(this);
  oxygen::ScopeGuard exit_frame([this] noexcept {
    should_open_ = false;
    should_close_ = false;
    PopID();
  });

  const std::string window_name = GetWindowName();
  if (should_open_) {
    OpenPopup(window_name.c_str());
  }
  is_opened_ = false;

  ApplyWindowGeometry();
  if (!BeginWindow(window_name)) {
    return;
  }
  is_opened_ = true;
  oxygen::ScopeGuard end_popup([] noexcept { EndPopup(); });

  DisplayState state;
  if (edit_directory_) {
    DrawPathEditor();
  } else {
    DrawPathBar();
  }
  DrawToolbar(state);
  DrawEntryList(state);
  if (state.pending_directory) {
    SetDirectory(*state.pending_directory);
  }
  DrawFileNameInput(state);
  HandleSelectAll(state);
  DrawActionButtons();
  DrawStatusText();
  DrawTypeFilterCombo();
}

inline void ImGui::FileBrowser::DrawPathEditor()
{
  if (focus_edit_directory_) {
    SetKeyboardFocusHere();
  }

  PushItemWidth(-1);
  const bool entered = InputText("##directory",
    current_directory_buffer_.data(), current_directory_buffer_.size(),
    file_browser_detail::CombineFlags(ImGuiInputTextFlags_CallbackResize,
      ImGuiInputTextFlags_EnterReturnsTrue, ImGuiInputTextFlags_AutoSelectAll),
    ExpandInputBuffer, &current_directory_buffer_);
  PopItemWidth();

  if (!IsItemActive() && !focus_edit_directory_) {
    edit_directory_ = false;
  }
  focus_edit_directory_ = false;

  if (!entered) {
    return;
  }

  const std::string_view text = BufferText(current_directory_buffer_);
  std::filesystem::path entered_directory
    = file_browser_detail::PathFromUtf8(text);
  std::error_code error;
  if (std::filesystem::is_directory(
        oxygen::base::ToNativePath(entered_directory), error)) {
    SetDirectory(entered_directory);
  } else if (std::filesystem::is_directory(
               oxygen::base::ToNativePath(entered_directory.parent_path()),
               error)) {
    SetDirectory(entered_directory.parent_path());
  } else {
    status_text_ = "[" + std::string(text) + "] is not a valid directory";
  }
}

inline void ImGui::FileBrowser::DrawPathBar()
{
#ifdef OXYGEN_WINDOWS
  DrawDriveSelector();
#endif

  DrawBreadcrumbs();

  if (HasFlag(FileBrowserFlags::kEditPathString)) {
    SameLine();
    if (SmallButton("#")) {
      BeginPathEdit();
    } else {
      ToolTip("Edit the current path");
    }
  }
}

inline void ImGui::FileBrowser::DrawBreadcrumbs()
{
  const std::filesystem::path root_name = current_directory_.root_name();
  const std::filesystem::path root_directory
    = current_directory_.root_directory();

  std::optional<std::filesystem::path> clicked;
  std::filesystem::path accumulated;
  int section_index = 0;
  for (const auto& part : current_directory_) {
    if (part.empty()) {
      continue;
    }
    accumulated /= part;
    // With a drive or UNC root name, the separator that follows is part of the
    // name's own button.
    if (!root_name.empty() && part == root_directory) {
      continue;
    }

    PushID(section_index);
    if (section_index > 0) {
      SameLine();
    }
    const std::string label = file_browser_detail::PathToUtf8(part);
    if (SmallButton(label.c_str())) {
      clicked = (!root_name.empty() && part == root_name)
        ? current_directory_.root_path()
        : accumulated;
    }
    PopID();
    ++section_index;
  }

  if (clicked) {
    SetDirectory(*clicked);
  }
}

inline void ImGui::FileBrowser::BeginPathEdit()
{
  const std::string text = file_browser_detail::PathToUtf8(current_directory_);
  current_directory_buffer_.assign(text.begin(), text.end());
  current_directory_buffer_.push_back('\0');
  edit_directory_ = true;
  focus_edit_directory_ = true;
}

inline void ImGui::FileBrowser::DrawToolbar(DisplayState& state)
{
  SameLine();
  DrawRefreshButton();
  if (HasFlag(FileBrowserFlags::kCreateNewDir)) {
    SameLine();
    DrawNewDirectoryButton(state);
  }
}

inline void ImGui::FileBrowser::DrawRefreshButton()
{
  if (!SmallButton("*")) {
    ToolTip("Refresh");
    return;
  }

#ifdef OXYGEN_WINDOWS
  drives_ = GetDrivesBitMask();
#endif

  RefreshFileRecords();

  // Keep the selected entries that still exist, and a typed new filename.
  std::set<std::filesystem::path> kept;
  for (const auto& name : selected_filenames_) {
    const bool exists = std::ranges::any_of(
      file_records_, [&name](const FileRecord& record) -> bool {
        return record.name == name;
      });
    if (exists) {
      kept.insert(name);
    }
  }
  if (HasFlag(FileBrowserFlags::kEnterNewFilename)) {
    if (const std::string_view typed = BufferText(input_name_buffer_);
      !typed.empty()) {
      kept.insert(file_browser_detail::PathFromUtf8(typed));
    }
  }
  selected_filenames_ = std::move(kept);
}

inline void ImGui::FileBrowser::DrawNewDirectoryButton(DisplayState& state)
{
  const std::string popup_name = "new dir##new_dir_" + instance_tag_.Value();
  if (SmallButton("+")) {
    OpenPopup(popup_name.c_str());
    new_directory_name_buffer_.front() = '\0';
  } else {
    ToolTip("Create a new directory");
  }

  if (!BeginPopup(popup_name.c_str())) {
    return;
  }
  oxygen::ScopeGuard end_popup([] noexcept { EndPopup(); });

  InputText("name", new_directory_name_buffer_.data(),
    new_directory_name_buffer_.size(), ImGuiInputTextFlags_CallbackResize,
    ExpandInputBuffer, &new_directory_name_buffer_);
  state.focus_on_input_text |= IsItemFocused();
  SameLine();

  if (Button("ok") && new_directory_name_buffer_.front() != '\0') {
    oxygen::ScopeGuard close_popup([] noexcept { CloseCurrentPopup(); });
    CreateDirectoryFromInput();
  }
}

inline void ImGui::FileBrowser::CreateDirectoryFromInput()
{
  const std::string_view name = BufferText(new_directory_name_buffer_);
  std::error_code error;
  const bool created = std::filesystem::create_directory(
    oxygen::base::ToNativePath(
      current_directory_ / file_browser_detail::PathFromUtf8(name)),
    error);
  if (created) {
    RefreshFileRecords();
    return;
  }
  status_text_ = "failed to create " + std::string(name);
  if (error) {
    status_text_ += ": " + error.message();
  }
}

inline void ImGui::FileBrowser::DrawEntryList(DisplayState& state)
{
  float reserved_height = GetFrameHeightWithSpacing();
  if (HasFlag(FileBrowserFlags::kEnterNewFilename)) {
    reserved_height += GetFrameHeightWithSpacing();
  }

  BeginChild("ch", ImVec2(0, -reserved_height), ImGuiChildFlags_Borders,
    HasFlag(FileBrowserFlags::kNoModal)
      ? ImGuiWindowFlags_AlwaysHorizontalScrollbar
      : ImGuiWindowFlags_None);
  oxygen::ScopeGuard end_child([] noexcept { EndChild(); });

  for (std::size_t index = 0; index < file_records_.size(); ++index) {
    DrawEntry(index, state);
  }
}

inline void ImGui::FileBrowser::DrawEntry(
  const std::size_t index, DisplayState& state)
{
  if (!IsEntryVisible(file_records_.at(index))) {
    return;
  }

  const bool selected
    = selected_filenames_.contains(file_records_.at(index).name);
  if (Selectable(file_records_.at(index).display_name.c_str(), selected,
        ImGuiSelectableFlags_NoAutoClosePopups)) {
    HandleEntryClicked(index);
  }

  if (IsMouseDoubleClicked(ImGuiMouseButton_Left)
    && IsItemHovered(ImGuiHoveredFlags_None)) {
    ActivateEntry(file_records_.at(index), state);
  } else if (IsKeyPressed(ImGuiKey_GamepadFaceDown) && IsItemHovered()) {
    if (file_records_.at(index).is_directory) {
      SetKeyboardFocusHere(-1);
    }
    ActivateEntry(file_records_.at(index), state);
  }
}

inline void ImGui::FileBrowser::ActivateEntry(
  const FileRecord& record, DisplayState& state)
{
  if (record.is_directory) {
    state.pending_directory = (record.utf8_name != "..")
      ? current_directory_ / record.name
      : current_directory_.parent_path();
    return;
  }
  if (!HasFlag(FileBrowserFlags::kSelectDirectory)) {
    selected_filenames_ = { record.name };
    is_ok_ = true;
    CloseCurrentPopup();
  }
}

inline void ImGui::FileBrowser::HandleEntryClicked(const std::size_t index)
{
  const FileRecord& record = file_records_.at(index);
  const bool selected = selected_filenames_.contains(record.name);
  const bool can_select = IsSelectable(record);
  const bool multi_enabled = HasFlag(FileBrowserFlags::kMultipleSelection)
    && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const bool range_select = can_select && multi_enabled && GetIO().KeyShift
    && range_selection_start_.has_value()
    && *range_selection_start_ < file_records_.size();
  const bool multi_select = !range_select && multi_enabled && GetIO().KeyCtrl;

  if (range_select) {
    SelectRange(*range_selection_start_, index);
    return;
  }

  if (selected) {
    if (multi_select) {
      selected_filenames_.erase(record.name);
      SetInputText("");
    } else {
      selected_filenames_ = { record.name };
      range_selection_start_ = index;
      SetInputText(record.utf8_name);
    }
    return;
  }

  if (!can_select) {
    return;
  }
  if (multi_select) {
    selected_filenames_.insert(record.name);
  } else {
    selected_filenames_ = { record.name };
  }
  SetInputText(record.utf8_name);
  range_selection_start_ = index;
}

inline void ImGui::FileBrowser::SelectRange(
  const std::size_t first, const std::size_t last)
{
  const auto [low, high] = std::minmax(first, last);
  selected_filenames_.clear();
  for (std::size_t i = low; i <= high; ++i) {
    if (IsSelectable(file_records_.at(i))) {
      selected_filenames_.insert(file_records_.at(i).name);
    }
  }
}

inline void ImGui::FileBrowser::DrawFileNameInput(DisplayState& state)
{
  if (!HasFlag(FileBrowserFlags::kEnterNewFilename)) {
    return;
  }

  if (input_name_buffer_.empty()) {
    input_name_buffer_.resize(1, '\0');
  }

  PushItemWidth(-1);
  if (InputText("##input_name", input_name_buffer_.data(),
        input_name_buffer_.size(), ImGuiInputTextFlags_CallbackResize,
        ExpandInputBuffer, &input_name_buffer_)) {
    const std::string_view typed = BufferText(input_name_buffer_);
    if (typed.empty()) {
      selected_filenames_.clear();
    } else {
      selected_filenames_ = { file_browser_detail::PathFromUtf8(typed) };
    }
  }
  state.focus_on_input_text |= IsItemFocused();
  PopItemWidth();
}

inline void ImGui::FileBrowser::HandleSelectAll(const DisplayState& state)
{
  if (state.focus_on_input_text || edit_directory_) {
    return;
  }
  const bool select_all = HasFlag(FileBrowserFlags::kMultipleSelection)
    && IsKeyPressed(ImGuiKey_A)
    && (IsKeyDown(ImGuiKey_LeftCtrl) || IsKeyDown(ImGuiKey_RightCtrl));
  if (!select_all) {
    return;
  }

  selected_filenames_.clear();
  for (const auto& record : file_records_) {
    if (IsSelectable(record)) {
      selected_filenames_.insert(record.name);
    }
  }
}

inline void ImGui::FileBrowser::DrawActionButtons()
{
  const bool confirm_on_enter = HasFlag(FileBrowserFlags::kConfirmOnEnter)
    && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
    && IsKeyPressed(ImGuiKey_Enter);

  if (!HasFlag(FileBrowserFlags::kSelectDirectory)) {
    BeginDisabled(selected_filenames_.empty());
    const bool ok = Button("ok");
    EndDisabled();
    if ((ok || confirm_on_enter) && !selected_filenames_.empty()) {
      is_ok_ = true;
      CloseCurrentPopup();
    }
  } else if (Button(" ok ") || confirm_on_enter) {
    is_ok_ = true;
    CloseCurrentPopup();
  }

  SameLine();

  const bool cancel = Button("cancel") || should_close_
    || (HasFlag(FileBrowserFlags::kCloseOnEsc)
      && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
      && IsKeyPressed(ImGuiKey_Escape));
  if (cancel) {
    CloseCurrentPopup();
  }
}

inline void ImGui::FileBrowser::DrawStatusText() const
{
  if (status_text_.empty() || HasFlag(FileBrowserFlags::kNoStatusBar)) {
    return;
  }

  SameLine();
  TextUnformatted(status_text_.c_str());
  if (IsItemHovered() && BeginTooltip()) {
    PushTextWrapPos(300.0F);
    TextUnformatted(status_text_.c_str());
    PopTextWrapPos();
    EndTooltip();
  }
}

inline void ImGui::FileBrowser::DrawTypeFilterCombo()
{
  if (type_filters_.empty()) {
    return;
  }

  SameLine();
  PushItemWidth(8 * GetFontSize());
  if (BeginCombo(
        "##type_filters", type_filters_.at(type_filter_index_).c_str())) {
    oxygen::ScopeGuard end_combo([] noexcept { EndCombo(); });

    for (std::size_t i = 0; i < type_filters_.size(); ++i) {
      const bool selected = i == type_filter_index_;
      if (Selectable(type_filters_.at(i).c_str(), selected) && !selected) {
        type_filter_index_ = i;
        ClearRangeSelectionState();
      }
    }
  }
  PopItemWidth();
}

inline auto ImGui::FileBrowser::HasSelected() const noexcept -> bool
{
  return is_ok_;
}

inline auto ImGui::FileBrowser::SetDirectory(
  const std::filesystem::path& directory) -> bool
{
  const std::filesystem::path previous = current_directory_;
  if (const auto error = TryApplyDirectory(directory)) {
    status_text_ = "error: " + *error;
  } else {
    return true;
  }

  // Stay where we were when possible, otherwise fall back to the default.
  const bool can_stay = !previous.empty() && previous != default_directory_
    && !TryApplyDirectory(previous).has_value();
  if (!can_stay) {
    ApplyDirectory(default_directory_);
  }
  return false;
}

inline auto ImGui::FileBrowser::TryApplyDirectory(
  const std::filesystem::path& directory) -> std::optional<std::string>
{
  try {
    ApplyDirectory(directory);
    return std::nullopt;
  } catch (const std::exception& error) {
    return std::string(error.what());
  }
}

inline auto ImGui::FileBrowser::GetDirectory() const noexcept
  -> const std::filesystem::path&
{
  return current_directory_;
}

inline auto ImGui::FileBrowser::GetSelected() const -> std::filesystem::path
{
  // When is_ok_ is true, the selection may be empty if kSelectDirectory is
  // enabled. Return the browsed directory in that case.
  if (selected_filenames_.empty()) {
    return current_directory_;
  }
  return current_directory_ / *selected_filenames_.begin();
}

inline auto ImGui::FileBrowser::GetMultiSelected() const
  -> std::vector<std::filesystem::path>
{
  if (selected_filenames_.empty()) {
    return { current_directory_ };
  }

  std::vector<std::filesystem::path> result;
  result.reserve(selected_filenames_.size());
  for (const auto& name : selected_filenames_) {
    result.push_back(current_directory_ / name);
  }
  return result;
}

inline void ImGui::FileBrowser::ClearSelected()
{
  selected_filenames_.clear();
  if (HasFlag(FileBrowserFlags::kEnterNewFilename)) {
    SetBufferText(input_name_buffer_, "");
  }
  is_ok_ = false;
}

inline void ImGui::FileBrowser::SetTypeFilters(
  const std::vector<std::string>& type_filters)
{
  type_filters_.clear();
  has_all_filter_ = false;
  type_filter_index_ = 0;

  std::vector<std::string> filters;
  for (const auto& raw_filter : type_filters) {
#ifdef OXYGEN_WINDOWS
    // Windows file names are case-insensitive.
    std::string filter = file_browser_detail::ToLowerAscii(raw_filter);
#else
    const std::string& filter = raw_filter;
#endif
    if (std::ranges::find(filters, filter) == filters.end()) {
      filters.push_back(filter);
    }
  }

  // Insert the generated filter matching every listed one.
  if (filters.size() > 1 && std::ranges::find(filters, ".*") == filters.end()) {
    std::string all_filters;
    for (const auto& filter : filters) {
      if (!all_filters.empty()) {
        all_filters += ',';
      }
      all_filters += filter;
    }
    has_all_filter_ = true;
    type_filters_.push_back(std::move(all_filters));
  }

  std::ranges::copy(filters, std::back_inserter(type_filters_));
  ClearRangeSelectionState();
}

inline void ImGui::FileBrowser::SetCurrentTypeFilterIndex(
  const std::size_t index)
{
  type_filter_index_
    = type_filters_.empty() ? 0 : (std::min)(index, type_filters_.size() - 1);
  ClearRangeSelectionState();
}

inline void ImGui::FileBrowser::SetInputName(const std::string_view input)
{
  DCHECK_F(HasFlag(FileBrowserFlags::kEnterNewFilename),
    "SetInputName can only be called when kEnterNewFilename is enabled");
  customized_input_name_ = input;
}

inline void ImGui::FileBrowser::SetInputText(const std::string_view text)
{
  if (HasFlag(FileBrowserFlags::kEnterNewFilename)) {
    SetBufferText(input_name_buffer_, text);
  }
}

inline void ImGui::FileBrowser::ToolTip(const char* const text)
{
  if (IsItemHovered() && BeginTooltip()) {
    TextUnformatted(text);
    EndTooltip();
  }
}

inline auto ImGui::FileBrowser::NormalizeDirectory(
  const std::filesystem::path& directory) -> std::filesystem::path
{
  auto normalized
    = oxygen::base::ToLogicalPath(std::filesystem::absolute(directory))
        .lexically_normal();
  // "dir/" would otherwise make parent_path() return "dir" itself.
  if (!normalized.has_filename() && normalized.has_relative_path()) {
    normalized = normalized.parent_path();
  }
  return normalized;
}

inline void ImGui::FileBrowser::UpdateFileRecords()
{
  file_records_.clear();
  file_records_.push_back(FileRecord {
    .is_directory = true,
    .name = "..",
    .utf8_name = "..",
    .display_name = "[D] ..",
    .extension = {},
    .sort_key = {},
  });

  // A directory that cannot be listed at all is always an error, so that the
  // caller can fall back. An entry that cannot be examined only is one when
  // kSkipItemsCausingError is off.
  const auto fail = [this](const std::error_code& error) -> void {
    status_text_ = "error: " + error.message();
    throw std::filesystem::filesystem_error(
      "cannot list directory", current_directory_, error);
  };
  const bool skip_errors = HasFlag(FileBrowserFlags::kSkipItemsCausingError);

  std::error_code error;
  std::filesystem::directory_iterator iterator(
    oxygen::base::ToNativePath(current_directory_), error);
  if (error) {
    fail(error);
  }

  for (const std::filesystem::directory_iterator end; iterator != end;
    iterator.increment(error)) {
    if (error) {
      // The iterator cannot continue past an error.
      if (!skip_errors) {
        fail(error);
      }
      status_text_ = "error: " + error.message();
      break;
    }

    const std::filesystem::directory_entry& entry = *iterator;
    const bool is_file = entry.is_regular_file(error);
    const bool is_directory = !error && !is_file && entry.is_directory(error);
    if (error) {
      if (!skip_errors) {
        fail(error);
      }
      status_text_ = "error: " + error.message();
      error.clear();
      continue;
    }
    if (!is_file && !is_directory) {
      continue;
    }

    std::filesystem::path name = entry.path().filename();
    std::string utf8_name = file_browser_detail::PathToUtf8(name);
    // Names starting with '$' are system entries, e.g. "$RECYCLE.BIN".
    if (utf8_name.empty() || utf8_name.front() == '$') {
      continue;
    }

    std::string extension = file_browser_detail::PathToUtf8(name.extension());
#ifdef OXYGEN_WINDOWS
    extension = file_browser_detail::ToLowerAscii(extension);
#endif
    std::string display_name = (is_directory ? "[D] " : "[F] ") + utf8_name;
    auto sort_key = file_browser_detail::MakeSortKey(is_directory, utf8_name);
    file_records_.push_back(FileRecord {
      .is_directory = is_directory,
      .name = std::move(name),
      .utf8_name = std::move(utf8_name),
      .display_name = std::move(display_name),
      .extension = std::move(extension),
      .sort_key = std::move(sort_key),
    });
  }

  // ".." stays first; everything else follows the sort key.
  std::ranges::sort(std::next(file_records_.begin()), file_records_.end(), {},
    &FileRecord::sort_key);

  ClearRangeSelectionState();
}

inline void ImGui::FileBrowser::RefreshFileRecords()
{
  try {
    UpdateFileRecords();
  } catch (const std::exception&) {
    // Re-enter the directory so that the fallback logic applies.
    SetDirectory(current_directory_);
  }
}

inline void ImGui::FileBrowser::ApplyDirectory(
  const std::filesystem::path& directory)
{
  current_directory_ = NormalizeDirectory(directory);
  UpdateFileRecords();

  // A pre-filled filename survives the directory change as long as the user
  // did not edit it.
  const bool keep_input_name = HasFlag(FileBrowserFlags::kEnterNewFilename)
    && selected_filenames_.size() == 1 && !customized_input_name_.empty()
    && BufferText(input_name_buffer_) == customized_input_name_;
  if (!keep_input_name) {
    selected_filenames_.clear();
    SetBufferText(input_name_buffer_, "");
  }
}

inline auto ImGui::FileBrowser::IsExtensionMatched(
  const std::string_view extension) const -> bool
{
  return file_browser_detail::MatchesTypeFilter(
    type_filters_, has_all_filter_, type_filter_index_, extension);
}

inline auto ImGui::FileBrowser::IsSelectable(const FileRecord& record) const
  -> bool
{
  const bool want_directory = HasFlag(FileBrowserFlags::kSelectDirectory);
  return record.utf8_name != ".." && record.is_directory == want_directory
    && (want_directory || IsExtensionMatched(record.extension));
}

inline auto ImGui::FileBrowser::IsEntryVisible(const FileRecord& record) const
  -> bool
{
  if (record.is_directory) {
    return true;
  }
  if (HasFlag(FileBrowserFlags::kHideRegularFiles)
    && HasFlag(FileBrowserFlags::kSelectDirectory)) {
    return false;
  }
  return IsExtensionMatched(record.extension);
}

inline void ImGui::FileBrowser::ClearRangeSelectionState()
{
  range_selection_start_.reset();
  for (std::size_t i = 1; i < file_records_.size(); ++i) {
    if (IsSelectable(file_records_.at(i))) {
      range_selection_start_ = i;
      return;
    }
  }
}

inline void ImGui::FileBrowser::SetBufferText(
  std::vector<char>& buffer, const std::string_view text)
{
  if (buffer.size() < text.size() + 1) {
    buffer.resize(text.size() + 1, '\0');
  }
  if (buffer.empty()) {
    return;
  }
  std::ranges::copy(text, buffer.begin());
  buffer.at(text.size()) = '\0';
}

inline auto ImGui::FileBrowser::BufferText(const std::vector<char>& buffer)
  -> std::string_view
{
  // ImGui keeps the text NUL-terminated inside the buffer.
  const auto end = std::ranges::find(buffer, '\0');
  return { buffer.data(), static_cast<std::size_t>(end - buffer.begin()) };
}

inline auto ImGui::FileBrowser::ExpandInputBuffer(
  ImGuiInputTextCallbackData* callback_data) -> int
{
  if (callback_data == nullptr
    || !file_browser_detail::HasImGuiFlag(
      callback_data->EventFlag, ImGuiInputTextFlags_CallbackResize)) {
    return 0;
  }

  auto* buffer = static_cast<std::vector<char>*>(callback_data->UserData);
  const auto required = static_cast<std::size_t>(callback_data->BufSize);
  std::size_t new_size = (std::max<std::size_t>)(buffer->size(), 1);
  while (new_size < required) {
    new_size *= 2;
  }
  buffer->resize(new_size, '\0');
  callback_data->Buf = buffer->data();
  return 0;
}

#ifdef OXYGEN_WINDOWS

inline auto ImGui::FileBrowser::GetDrivesBitMask() -> std::uint32_t
{
  std::uint32_t mask = 0;
  for (unsigned int i = 0; i < 26; ++i) {
    const std::string root { static_cast<char>('A' + i), ':', '\\' };
    // Invalid or inaccessible drives, e.g. empty CD drives or disconnected
    // network shares, report an error and are simply not listed.
    std::error_code error;
    if (std::filesystem::exists(root, error) && !error) {
      mask |= 1U << i;
    }
  }
  return mask;
}

inline void ImGui::FileBrowser::DrawDriveSelector()
{
  // UNC paths have no drive letter.
  const std::string root_name
    = file_browser_detail::PathToUtf8(current_directory_.root_name());
  if (root_name.size() != 2 || root_name.back() != ':') {
    return;
  }
  const char first_letter = root_name.front();
  const char current_drive = (first_letter >= 'a' && first_letter <= 'z')
    ? static_cast<char>(first_letter - ('a' - 'A'))
    : first_letter;

  PushItemWidth(4 * GetFontSize());
  if (BeginCombo(
        "##select_drive", (std::string { current_drive, ':' }).c_str())) {
    oxygen::ScopeGuard end_combo([] noexcept { EndCombo(); });

    for (unsigned int i = 0; i < 26; ++i) {
      if ((drives_ & (1U << i)) == 0U) {
        continue;
      }

      const char drive = static_cast<char>('A' + i);
      const bool selected = current_drive == drive;
      if (Selectable((std::string { drive, ':' }).c_str(), selected)
        && !selected) {
        SetDirectory(std::string { drive, ':', '\\' });
      }
    }
  }
  PopItemWidth();

  SameLine();
}

#endif // OXYGEN_WINDOWS
