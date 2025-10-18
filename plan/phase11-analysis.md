---
goal: Phase 11 - XAML Binding and UI Integration for Window Decoration System
version: 1.0
date_created: 2025-01-18
last_updated: 2025-01-18
owner: Aura Module Team
status: 'Planned'
tags: [feature, aura, window-decoration, xaml, ui-integration, phase-11]
---

# Phase 11: XAML Binding and UI Integration Analysis

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

## 1. Executive Summary

Phase 11 integrates the Window Decoration System (implemented in Phases 1-10) with the MainShellView XAML UI. This phase transforms the existing hardcoded MainShellView chrome into a data-driven system that binds to `WindowDecorationOptions` from the `WindowContext`.

**Key Objectives:**

1. Replace hardcoded MainShellView chrome with data-driven bindings to WindowDecorationOptions
2. Support all decoration features: title bar configuration, button visibility/placement, menu types, backdrop
3. Maintain existing functionality: system menu button, MenuBar, ExpandableMenuBar, MenuButton, window control buttons
4. Ensure clean architecture: no hacks, respect existing contracts, leverage WinUI 3 platform features

## 2. Current State Analysis

### 2.1 Existing MainShellView Architecture

**MainShellView.xaml Structure:**

```text
UserControl (Root)
└── Grid (RootGrid)
    ├── Grid (CustomTitleBar) - Row 0
    │   ├── ColumnDefinition (LeftPaddingColumn) - System inset
    │   ├── ColumnDefinition (IconColumn) - Window icon
    │   ├── ColumnDefinition (PrimaryCommandsColumn) - Main menu
    │   ├── ColumnDefinition (DragColumn) - Drag region
    │   ├── ColumnDefinition (SecondaryCommandsColumn) - Settings menu button
    │   └── ColumnDefinition (RightPaddingColumn) - System inset
    └── Grid (ContentLayer) - Row 1
        └── RouterOutlet - Main content
```

**Current Title Bar Features:**

- **Fixed height**: Determined by `appWindow.TitleBar.Height`
- **System insets**: Left/Right padding columns for system overlays
- **Window icon**: ImageIcon showing DroidNet.png
- **Primary menu**: StackPanel with MenuBar bound to `ViewModel.MainMenu`
- **Drag region**: DragColumn with MinWidth=48
- **Secondary menu**: StackPanel with MenuButton bound to `ViewModel.SettingsMenu`
- **Passthrough regions**: PrimaryCommands and SecondaryCommands marked as non-draggable
- **Adaptive behavior**: SecondaryCommands visibility controlled by MinWindowWidth

**Code-Behind (MainShellView.xaml.cs):**

- `SetupCustomTitleBar()`: Called on Loaded/SizeChanged with 100ms throttling
- Configures system title bar height and insets
- Calculates passthrough regions for interactive elements
- Computes MinWindowWidth based on all chrome elements

**ViewModel (MainShellViewModel.cs):**

- **MainMenu**: Resolved from WindowContext.MenuSource or fallback to SettingsMenu
- **SettingsMenu**: Fallback menu with Settings and Themes options
- **UpdateMenuFromWindowContext()**: Looks up WindowContext from WindowManagerService
- **SetupWindowTitleBar()**: Sets `ExtendsContentIntoTitleBar = true` and `PreferredHeightOption = Standard`

### 2.2 Menu System Integration

The solution uses three menu control types from `DroidNet.Controls.Menus`:

1. **MenuBar** (Horizontal bar with root items)
   - Used in PrimaryCommands for full application menu
   - Bound to `ViewModel.MainMenu`
   - Suitable for standard persistent menus

2. **ExpandableMenuBar** (Hamburger → MenuBar)
   - Compact hamburger button that expands to full MenuBar
   - Used for space-constrained scenarios
   - State management: Collapsed/Expanded visual states
   - **Not currently used in MainShellView but available**

3. **MenuButton** (Flyout trigger)
   - Used in SecondaryCommands for Settings/Themes
   - Bound to `ViewModel.SettingsMenu`
   - Properties: `Chrome="Transparent"`, `CornerRadius="5"`

**Menu Source Resolution:**

- WindowContext.MenuSource created from IMenuProvider during window initialization
- MainShellViewModel.UpdateMenuFromWindowContext() retrieves menu from WindowContext
- Graceful degradation: Falls back to SettingsMenu if no provider registered

### 2.3 Window Categories and Default Decorations

**Predefined Categories:**

- **Main**: Primary application window (height=40, all buttons, Mica backdrop)
- **Secondary**: Additional windows (standard height, all buttons, MicaAlt backdrop)
- **Document**: Content editing (standard height, all buttons, Mica backdrop)
- **Tool**: Palettes/inspectors (height=32, no maximize, MicaAlt backdrop)
- **Transient**: Floating UI (standard height, no maximize, Acrylic backdrop)
- **Modal**: Dialogs (standard height, no maximize, Acrylic backdrop)
- **System**: No Aura chrome (ChromeEnabled=false)

**Default Backdrop Assignment:**

```csharp
Main → Mica
Secondary → MicaAlt
Document → Mica
Tool → MicaAlt
Transient → Acrylic
Modal → Acrylic
System → None
```

### 2.4 WindowDecorationOptions Structure

```csharp
public sealed record WindowDecorationOptions
{
    public required WindowCategory Category { get; init; }
    public bool ChromeEnabled { get; init; } = true;
    public TitleBarOptions TitleBar { get; init; } = TitleBarOptions.Default;
    public WindowButtonsOptions Buttons { get; init; } = WindowButtonsOptions.Default;
    public MenuOptions? Menu { get; init; }
    public BackdropKind Backdrop { get; init; } = BackdropKind.None;
    public bool EnableSystemTitleBarOverlay { get; init; }
}

public sealed record TitleBarOptions
{
    public double Height { get; init; } = 32.0;
    public double Padding { get; init; } = 8.0;
    public bool ShowTitle { get; init; } = true;
    public bool ShowIcon { get; init; } = true;
    public DragRegionBehavior DragBehavior { get; init; } = DragRegionBehavior.Default;
}

public sealed record WindowButtonsOptions
{
    public bool ShowMinimize { get; init; } = true;
    public bool ShowMaximize { get; init; } = true;
    public bool ShowClose { get; init; } = true;
    public ButtonPlacement Placement { get; init; } = ButtonPlacement.Right;
}

public sealed record MenuOptions
{
    public required string MenuProviderId { get; init; }
    public bool IsCompact { get; init; }
}
```

## 3. Design Decisions

### 3.1 Binding Architecture

**Decision: Bind MainShellView directly to WindowContext.Decoration**

**Rationale:**

- WindowContext is the single source of truth for window metadata
- Decoration is already populated during window creation
- Avoids duplicating decoration state in MainShellViewModel
- Enables clean separation of concerns

**Implementation:**

- MainShellViewModel exposes `WindowContext? Context { get; private set; }`
- MainShellView binds to `{x:Bind ViewModel.Context.Decoration.*}`
- Context populated in ActivationComplete event handler

**Alternative Rejected:** Copying decoration properties to MainShellViewModel

- Violates DRY principle
- Requires synchronization logic
- Increases maintenance burden

### 3.2 Menu Rendering Strategy

**Decision: Use MenuBar for persistent menus, ExpandableMenuBar for compact mode**

**Rationale:**

- MenuOptions.IsCompact explicitly controls menu presentation
- ExpandableMenuBar provides space-efficient hamburger → bar expansion
- MenuBar remains the standard for full-width menus
- Both controls consume IMenuSource from WindowContext

**Implementation:**

```xaml
<!-- Persistent menu (IsCompact=false or null) -->
<cm:MenuBar
    Visibility="{x:Bind ViewModel.Context.Decoration.Menu, Converter={StaticResource NullToVisibilityConverter}}"
    MenuSource="{x:Bind ViewModel.Context.MenuSource}" />

<!-- Compact menu (IsCompact=true) -->
<cm:ExpandableMenuBar
    Visibility="{x:Bind ViewModel.Context.Decoration.Menu.IsCompact, Converter={StaticResource BoolToVisibilityConverter}}"
    MenuSource="{x:Bind ViewModel.Context.MenuSource}" />
```

**Alternative Rejected:** Single control with template switching

- ExpandableMenuBar and MenuBar have different interaction models
- Template switching adds complexity
- Explicit controls are clearer

### 3.3 Window Control Buttons Strategy

**Decision: Keep existing WinUI caption buttons, control visibility via Aura chrome**

**Rationale:**

- WinUI 3 provides caption buttons automatically when `ExtendsContentIntoTitleBar = true`
- WinUI buttons respect Windows 11 design system (rounded corners, animations)
- WindowDecorationOptions.ChromeEnabled controls whether Aura chrome is used
- When ChromeEnabled=false, system chrome takes over entirely

**Implementation:**

- No custom button controls needed in XAML
- WinUI manages button placement based on TitleBar.LeftInset/RightInset
- Buttons automatically positioned in RightPaddingColumn
- ShowMinimize/ShowMaximize/ShowClose not directly bound (WinUI handles via ExtendsContentIntoTitleBar)

**Note on ButtonPlacement:**

- ButtonPlacement enum (Left/Right/Auto) is for future extensibility
- Phase 11 uses WinUI default (Right on Windows)
- Custom button positioning deferred to Phase 12+

**Alternative Rejected:** Custom button controls

- Reinvents wheel unnecessarily
- Loses Windows 11 native styling
- Increases maintenance burden
- WinUI buttons already respect theme, DPI, accessibility

### 3.4 System Menu Button Strategy

**Decision: No explicit system menu button in Phase 11**

**Rationale:**

- System menu (window icon double-click) is Windows OS feature
- Handled automatically by WinUI when window has icon
- Not part of WindowDecorationOptions specification
- TitleBarOptions.ShowIcon controls window icon visibility
- Icon serves as system menu trigger per Windows convention

**Implementation:**

- Window icon shown/hidden via TitleBarOptions.ShowIcon binding
- Icon placement in IconColumn (Grid.Column=1)
- System menu functionality provided by OS
- No custom code required

**Alternative Rejected:** Custom system menu button

- Unnecessary duplication of OS functionality
- Complicates chrome implementation
- Not in specification requirements

### 3.5 Title Bar Height Configuration

**Decision: Bind CustomTitleBar.Height to Decoration.TitleBar.Height**

**Rationale:**

- WindowDecorationOptions.TitleBar.Height is definitive
- Overrides WinUI's appWindow.TitleBar.Height calculation
- Enables per-window title bar sizing
- Simplifies SetupCustomTitleBar() logic

**Implementation:**

```xaml
<Grid x:Name="CustomTitleBar"
      Height="{x:Bind ViewModel.Context.Decoration.TitleBar.Height, Mode=OneWay}">
```

**Code-behind updates:**

- Remove dynamic height calculation from SetupCustomTitleBar()
- Keep inset calculations (LeftPaddingColumn/RightPaddingColumn)
- Keep passthrough region setup
- Height is now purely data-driven

### 3.6 Drag Region Behavior

**Decision: Use DragRegionBehavior.Default for Phase 11, defer Custom implementation**

**Rationale:**

- Default behavior (entire title bar draggable except interactive elements) covers 90% of cases
- Current passthrough region logic already implements Default behavior
- Custom drag regions require platform-specific APIs
- Extended and None behaviors can be added incrementally

**Implementation:**

- Keep existing InputNonClientPointerSource.SetRegionRects() logic
- PrimaryCommands and SecondaryCommands marked as NonClientRegionKind.Passthrough
- DragColumn remains draggable
- DragBehavior enum available for future extensibility

**Future Work (Phase 12+):**

- DragRegionBehavior.Custom: App provides exact regions
- DragRegionBehavior.Extended: Expand drag area beyond title bar
- DragRegionBehavior.None: Disable all dragging

### 3.7 Chrome Visibility Strategy

**Decision: Use ChromeEnabled to toggle entire custom title bar row**

**Rationale:**

- ChromeEnabled=false means "use system chrome entirely"
- When false, CustomTitleBar should be hidden
- System title bar automatically shown by OS
- Simplifies logic: all or nothing

**Implementation:**

```xaml
<Grid x:Name="CustomTitleBar"
      Visibility="{x:Bind ViewModel.Context.Decoration.ChromeEnabled, Converter={StaticResource BoolToVisibilityConverter}}">
```

**When ChromeEnabled=false:**

- CustomTitleBar collapsed
- ExtendsContentIntoTitleBar should be false
- System provides standard title bar
- ValidationException prevents Menu when ChromeEnabled=false

### 3.8 Settings Menu Button Behavior

**Decision: Keep SecondaryCommands with SettingsMenu, independent of WindowContext.MenuSource**

**Rationale:**

- SettingsMenu is application-level, not window-specific
- Provides Settings and Themes regardless of window decoration
- SecondaryCommands already has adaptive visibility logic
- MenuButton Chrome="Transparent" style maintained

**Implementation:**

- No changes to SecondaryCommands binding
- Continues to use `ViewModel.SettingsMenu`
- Visibility controlled by MinWindowWidth trigger (existing logic)
- Independent of WindowDecorationOptions.Menu

**Alternative Rejected:** Hide SettingsMenu when no decoration menu

- Settings/Themes are always relevant
- User expects consistent access to app-level settings
- Decoration menu is content-specific, not app-level

## 4. Implementation Plan

### 4.1 ViewModel Changes

**MainShellViewModel.cs:**

1. **Add WindowContext property:**

```csharp
/// <summary>
/// Gets the window context associated with this view model.
/// </summary>
public WindowContext? Context { get; private set; }
```

2. **Update ActivationComplete handler:**

```csharp
_ = router.Events.OfType<ActivationComplete>()
    .Take(1)
    .Subscribe(@event =>
    {
        this.Window = (Window)@event.Context.NavigationTarget;

        // Look up WindowContext
        if (this.windowManagerService is not null)
        {
            this.Context = this.windowManagerService.OpenWindows
                .FirstOrDefault(wc => ReferenceEquals(wc.Window, this.Window));
        }

        this.SetupWindowTitleBar();
        this.UpdateMenuFromWindowContext();
    });
```

3. **Update SetupWindowTitleBar():**

```csharp
private void SetupWindowTitleBar()
{
    Debug.Assert(this.Window is not null, "an activated ViewModel must always have a Window");

    // Only extend content if chrome is enabled
    var chromeEnabled = this.Context?.Decoration?.ChromeEnabled ?? true;
    this.Window.ExtendsContentIntoTitleBar = chromeEnabled;

    if (chromeEnabled)
    {
        this.Window.AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Standard;
    }
}
```

4. **Keep existing methods unchanged:**

- InitializeSettingsMenu()
- UpdateMenuFromWindowContext()
- Theme management logic

### 4.2 XAML Changes

**MainShellView.xaml:**

1. **Add converters to Resources (use CommunityToolkit converters):**

```xaml
<UserControl.Resources>
    <!-- Existing CompactButton styles -->

    <!-- BoolToVisibilityConverter already exists -->
    <ctkcvt:BoolToVisibilityConverter x:Key="BoolToVis" />

    <!-- Use EmptyObjectToObjectConverter for null checks -->
    <ctkcvt:EmptyObjectToObjectConverter x:Key="NullToVis"
                                          EmptyValue="Collapsed"
                                          NotEmptyValue="Visible" />

    <!-- Use BoolToObjectConverter for IsCompact checks -->
    <ctkcvt:BoolToObjectConverter x:Key="IsCompactToVis"
                                   TrueValue="Visible"
                                   FalseValue="Collapsed" />

    <ctkcvt:BoolToObjectConverter x:Key="IsNotCompactToVis"
                                   TrueValue="Collapsed"
                                   FalseValue="Visible" />
</UserControl.Resources>
```

2. **Update CustomTitleBar Grid:**

```xaml
<Grid x:Name="CustomTitleBar"
      Background="Transparent"
      Height="{x:Bind ViewModel.Context.Decoration.TitleBar.Height, Mode=OneWay, FallbackValue=32}"
      Visibility="{x:Bind ViewModel.Context.Decoration.ChromeEnabled, Mode=OneWay, Converter={StaticResource BoolToVis}}">
```

3. **Update IconColumn:**

```xaml
<ImageIcon
    Grid.Column="1"
    Height="20"
    Margin="4"
    VerticalAlignment="Center"
    Source="/Assets/DroidNet.png"
    Visibility="{x:Bind ViewModel.Context.Decoration.TitleBar.ShowIcon, Mode=OneWay, Converter={StaticResource BoolToVis}}" />
```

4. **Update PrimaryCommands with menu type switching:**

```xaml
<StackPanel
    x:Name="PrimaryCommands"
    Grid.Column="2"
    Margin="{x:Bind ViewModel.Context.Decoration.TitleBar.Padding, Mode=OneWay, FallbackValue=8}"
    VerticalAlignment="Center"
    Background="Transparent"
    Orientation="Horizontal"
    Spacing="12"
    Visibility="{x:Bind ViewModel.Context.Decoration.Menu, Mode=OneWay, Converter={StaticResource NullToVis}}">

    <!-- Standard persistent menu -->
    <cm:MenuBar
        VerticalAlignment="Center"
        MenuSource="{x:Bind ViewModel.Context.MenuSource, Mode=OneWay}"
        Visibility="{x:Bind ViewModel.Context.Decoration.Menu.IsCompact, Mode=OneWay, Converter={StaticResource IsNotCompactToVis}}" />

    <!-- Compact expandable menu -->
    <cm:ExpandableMenuBar
        VerticalAlignment="Center"
        MenuSource="{x:Bind ViewModel.Context.MenuSource, Mode=OneWay}"
        Visibility="{x:Bind ViewModel.Context.Decoration.Menu.IsCompact, Mode=OneWay, Converter={StaticResource IsCompactToVis}}" />
</StackPanel>
```

5. **Keep SecondaryCommands unchanged:**

```xaml
<StackPanel
    x:Name="SecondaryCommands"
    Grid.Column="4"
    Margin="0,0,10,0"
    VerticalAlignment="Center"
    Background="Transparent"
    Orientation="Horizontal"
    Visibility="Collapsed">
    <cm:MenuButton
        Chrome="Transparent"
        CornerRadius="5"
        MenuSource="{x:Bind ViewModel.SettingsMenu, Mode=OneWay}">
        <FontIcon FontSize="16" Glyph="&#xE713;" />
    </cm:MenuButton>
</StackPanel>
```

6. **Keep existing VisualStateManager for adaptive layout**

### 4.3 Code-Behind Changes

**MainShellView.xaml.cs:**

1. **Update SetupCustomTitleBar():**

```csharp
private void SetupCustomTitleBar()
{
    Debug.Assert(
        this.ViewModel?.Window is not null,
        "expecting a properly setup ViewModel when loaded");

    var appWindow = this.ViewModel.Window.AppWindow;
    var scaleAdjustment = this.CustomTitleBar.XamlRoot.RasterizationScale;

    // Note: Height now controlled by binding to Decoration.TitleBar.Height
    // Only configure insets
    this.LeftPaddingColumn.Width = new GridLength(appWindow.TitleBar.LeftInset / scaleAdjustment);
    this.RightPaddingColumn.Width = new GridLength(appWindow.TitleBar.RightInset / scaleAdjustment);

    // Configure passthrough regions for interactive elements
    var transform = this.PrimaryCommands.TransformToVisual(visual: null);
    var bounds = transform.TransformBounds(
        new Rect(0, 0, this.PrimaryCommands.ActualWidth, this.PrimaryCommands.ActualHeight));
    var primaryCommandsRect = GetRect(bounds, scaleAdjustment);

    transform = this.SecondaryCommands.TransformToVisual(visual: null);
    bounds = transform.TransformBounds(
        new Rect(0, 0, this.SecondaryCommands.ActualWidth, this.SecondaryCommands.ActualHeight));
    var secondaryCommandsRect = GetRect(bounds, scaleAdjustment);

    var rectArray = new[] { primaryCommandsRect, secondaryCommandsRect };

    var nonClientInputSrc = InputNonClientPointerSource.GetForWindowId(appWindow.Id);
    nonClientInputSrc.SetRegionRects(NonClientRegionKind.Passthrough, rectArray);

    // Calculate minimum window width
    this.MinWindowWidth = this.LeftPaddingColumn.Width.Value + this.IconColumn.ActualWidth +
                          this.PrimaryCommands.ActualWidth + this.DragColumn.MinWidth +
                          this.SecondaryCommands.ActualWidth +
                          this.RightPaddingColumn.Width.Value;
}
```

2. **Keep existing reactive setup unchanged**

### 4.4 Converters Strategy

**Use CommunityToolkit.WinUI.Converters instead of creating custom converters:**

All required converters are available from CommunityToolkit.WinUI.Converters:

1. **BoolToVisibilityConverter** - Already in use, converts bool to Visibility
   - Already declared in MainShellView.xaml resources

2. **EmptyObjectToObjectConverter** - Converts null/empty objects to specified values
   - Use for MenuOptions null checks
   - Configuration: `EmptyValue="Collapsed"`, `NotEmptyValue="Visible"`

3. **BoolToObjectConverter** - Converts bool to any object type
   - Use for IsCompact property checks
   - Two instances needed:
     - `IsCompactToVis`: `TrueValue="Visible"`, `FalseValue="Collapsed"`
     - `IsNotCompactToVis`: `TrueValue="Collapsed"`, `FalseValue="Visible"`

**No custom converter creation required** - all functionality provided by CommunityToolkit!

**No custom converter creation required** - all functionality provided by CommunityToolkit!

## 5. Testing Strategy

### 5.1 Unit Tests

**Test file:** `projects/Aura/tests/Converters/ConverterTests.cs`

**Note:** Since we're using CommunityToolkit converters, unit tests should focus on verifying:
1. Correct converter configuration in XAML resources
2. Binding expressions using converters work correctly
3. Integration tests (below) cover the actual behavior

Basic validation tests:

1. **EmptyObjectToObjectConverter configuration:**
   - Verify EmptyValue and NotEmptyValue are set correctly in resources

2. **BoolToObjectConverter instances:**
   - Verify IsCompactToVis has correct TrueValue/FalseValue
   - Verify IsNotCompactToVis has correct TrueValue/FalseValue

### 5.2 UI Integration Tests

**Test file:** `projects/Aura/tests/Views/MainShellViewDecorationTests.cs`

1. **Title bar height binding:**
   - Decoration with Height=40 renders 40px title bar
   - Decoration with Height=32 renders 32px title bar
   - Null decoration uses fallback (32px)

2. **Icon visibility:**
   - ShowIcon=true → icon visible
   - ShowIcon=false → icon collapsed

3. **Menu rendering:**
   - Menu=null → PrimaryCommands collapsed
   - Menu with IsCompact=false → MenuBar visible, ExpandableMenuBar collapsed
   - Menu with IsCompact=true → ExpandableMenuBar visible, MenuBar collapsed

4. **Chrome visibility:**
   - ChromeEnabled=true → CustomTitleBar visible, ExtendsContentIntoTitleBar=true
   - ChromeEnabled=false → CustomTitleBar collapsed, ExtendsContentIntoTitleBar=false

5. **Settings menu independence:**
   - SecondaryCommands always shows SettingsMenu
   - Visibility controlled by MinWindowWidth (existing logic)

6. **Drag region behavior:**
   - PrimaryCommands marked as passthrough
   - SecondaryCommands marked as passthrough
   - DragColumn remains draggable

### 5.3 Manual Testing Scenarios

1. **Main window with full menu:**
   - Category: Main
   - Menu: "App.MainMenu", IsCompact=false
   - Verify: MenuBar visible, height=40px, all buttons, Mica backdrop

2. **Tool window with compact menu:**
   - Category: Tool
   - Menu: "App.ToolMenu", IsCompact=true
   - Verify: ExpandableMenuBar visible (hamburger), height=32px, no maximize button

3. **Document window with no icon:**
   - Category: Document
   - Menu: "App.DocumentMenu"
   - TitleBar.ShowIcon: false
   - Verify: Icon collapsed, standard height, MenuBar visible

4. **System window (no chrome):**
   - Category: System
   - ChromeEnabled: false
   - Verify: CustomTitleBar collapsed, system title bar shown

5. **Narrow window adaptive behavior:**
   - Resize main window below MinWindowWidth
   - Verify: SecondaryCommands collapses (existing behavior maintained)

## 6. Architectural Principles

### 6.1 Data-Driven Design

**Principle:** Configuration drives UI, not code.

**Application:**

- All chrome properties bound to WindowDecorationOptions
- No hardcoded chrome values in XAML
- ViewModel exposes WindowContext, not decoration fragments
- FallbackValue attributes for design-time rendering

### 6.2 Clean Code

**Principle:** No hacks, leverage platform capabilities.

**Application:**

- Use WinUI 3 caption buttons (ExtendsContentIntoTitleBar)
- Use InputNonClientPointerSource for passthrough regions (existing)
- Use standard XAML bindings and converters
- Respect WindowDecorationOptions validation rules

### 6.3 Separation of Concerns

**Principle:** Each component has a single responsibility.

**Application:**

- **WindowContext**: Stores window metadata and decoration
- **MainShellViewModel**: Coordinates window lifecycle and menus
- **MainShellView.xaml**: Declarative UI bindings
- **MainShellView.xaml.cs**: Platform integration (insets, passthrough)
- **Converters**: Pure transformation logic

### 6.4 Backward Compatibility

**Principle:** Maintain existing contracts and behavior.

**Application:**

- IWindowManagerService API unchanged
- WindowContext contract unchanged
- SettingsMenu remains independent
- MinWindowWidth adaptive logic preserved
- Reactive setup (Loaded/SizeChanged throttling) preserved

### 6.5 Graceful Degradation

**Principle:** Handle missing data without crashing.

**Application:**

- FallbackValue for decoration properties
- Null checks for WindowContext
- ChromeEnabled=false falls back to system chrome
- Missing menu provider → no menu (existing behavior)

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **Binding failure for nested properties** | Chrome not rendered | Use FallbackValue, null-conditional bindings |
| **Converter logic errors** | Incorrect visibility | Comprehensive unit tests for all converters |
| **ExpandableMenuBar not tested** | Compact menus broken | Add ExpandableMenuBar to UI test suite |
| **Performance regression (reactive setup)** | Slow title bar updates | Keep existing 100ms throttling, profile SetupCustomTitleBar() |
| **System inset calculation breaks** | Overlapping controls | Preserve GetRect() logic, test on multi-monitor setup |
| **ButtonPlacement not implemented** | User expects Left placement | Document as Phase 12+ work, validation prevents invalid values |

## 8. Future Work (Post-Phase 11)

### 8.1 Phase 12: Advanced Drag Regions

- Implement DragRegionBehavior.Custom (app-provided regions)
- Implement DragRegionBehavior.Extended (beyond title bar)
- Implement DragRegionBehavior.None (disable dragging)
- Update SetupCustomTitleBar() to respect DragBehavior

### 8.2 Phase 13: Custom Button Placement

- Implement ButtonPlacement.Left (macOS-style)
- Implement ButtonPlacement.Auto (platform-aware)
- Custom button controls or WinUI API extensions
- Update XAML to reposition caption buttons

### 8.3 Phase 14: Dynamic Title Bar Content

- Support for custom title bar templates
- Title TextBlock with binding to WindowContext.Title
- Title visibility controlled by ShowTitle
- Title truncation and tooltip

### 8.4 Phase 15: Per-Window Decoration Overrides

- UI for editing window decorations at runtime
- Save custom decorations to WindowDecorationSettings
- Per-window-ID overrides in settings
- Live update on settings change via IOptionsMonitor

### 8.5 Phase 16: Theme Coordination

- WindowBackdropService integration with IAppearanceSettings
- Application-wide backdrop defaults
- Per-window backdrop overrides
- Smooth backdrop transitions on theme change

## 9. Acceptance Criteria

**Phase 11 is complete when:**

1. ✅ MainShellViewModel exposes `WindowContext? Context { get; private set; }`
2. ✅ MainShellView.xaml binds CustomTitleBar.Height to `Context.Decoration.TitleBar.Height`
3. ✅ Icon visibility controlled by `Context.Decoration.TitleBar.ShowIcon`
4. ✅ PrimaryCommands visibility controlled by `Context.Decoration.Menu != null`
5. ✅ MenuBar shown when `Menu.IsCompact == false`
6. ✅ ExpandableMenuBar shown when `Menu.IsCompact == true`
7. ✅ CustomTitleBar visibility controlled by `Context.Decoration.ChromeEnabled`
8. ✅ SecondaryCommands (SettingsMenu) remains independent of WindowContext
9. ✅ All converters have unit tests with ≥90% coverage
10. ✅ UI integration tests pass for all window categories
11. ✅ Manual testing confirms: Main, Tool, Document, System windows render correctly
12. ✅ No breaking changes to existing IWindowManagerService, WindowContext, or MainShellViewModel APIs
13. ✅ Documentation updated in window-decorations-spec.md

## 10. References

- **Specification:** `plan/window-decorations-spec.md`
- **Implementation Plan:** `plan/window-decoration-plan.md`
- **WinUI 3 Docs:** [ExtendsContentIntoTitleBar](https://learn.microsoft.com/en-us/windows/apps/develop/title-bar)
- **Menu Controls:** `projects/Controls/Menus/src/`
- **WindowContext:** `projects/Aura/src/WindowManagement/WindowContext.cs`
- **WindowDecorationOptions:** `projects/Aura/src/Decoration/WindowDecorationOptions.cs`
