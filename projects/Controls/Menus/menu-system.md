# DroidNet Menu System Documentation

This document describes the menu system components provided by `DroidNet.Controls.Menus`, all built on the unified `MenuBuilder` and `MenuItemData` architecture.

## Custom Menu System Overview

**Philosophy**: Data-first composition where `MenuBuilder` materializes neutral menu hierarchies and reusable `IMenuSource` instances that are projected by dedicated templated controls. The custom item control is already implemented; container work is underway with WinUI primitives used as interim hosts while bespoke containers come online.

### Design Tenets

- **Data-first builder flow** – `MenuBuilder` focuses purely on producing `MenuItemData` hierarchies and an `IMenuSource`, with all WinUI-specific logic removed from its public surface.
- **Deterministic identifier pipeline** – IDs are normalized, uniquified, and applied top-down so lookup behavior stays predictable across rebuilds.
- **Centralized traversal helpers** – private helpers own subtree realization, enumeration, and identifier application to keep the fluent API terse and correct.
- **Lean, cached lookup services** – `MenuServices` snapshots the hierarchy on demand with builder-driven dirty tracking, providing fast lookups and radio-group coordination.
- **Radio-group safety** – selection logic flips only the relevant items, guaranteeing single-selection semantics without exposing extra APIs.
- **Reusable menu source** – `MenuSource` pairs the realized `ObservableCollection<MenuItemData>` with the services instance and is reused across successive `Build()` calls to avoid churn.
- **Test scaffolding ready** – the current structure keeps responsibilities isolated so unit tests can focus on ID regeneration, lookup behavior, and group logic without UI dependencies.

### 1. Custom MenuItem (✅ Implemented)

**What it is**: The `DroidNet.Controls.MenuItem` custom control that renders the four-column layout (Icon | Text | Accelerator | State).
**Usage**: Used internally by menu containers via `ItemsRepeater`/`ListView` templates or direct composition.
**Presentation**: Implements the Icon|Text|Accelerator|State column contract with complete visual state management.
**Highlights**:

- Rich visual states (hover, pressed, disabled, keyboard-active) and separator support
- Event surface for `Invoked`, hover change, submenu requests, and radio-group coordination
- Keyboard support (Enter/Space execution, arrow navigation hooks)
- Verified by automated UI tests in `Controls.Menus.UI.Tests/MenuItemTests.cs`

**Status**: ✅ **Implemented and theming-complete (foundation is production-ready)**

### 2. Custom MenuBar (🚧 Template Design In Progress)

**What it is**: A templated control that composes `MenuItem` instances into a horizontal root bar while driving behavior through `MenuSource.Services`.
**Usage**: Planned API `<controls:MenuBar MenuSource="{x:Bind ViewModel.MainMenu}" />` where `MainMenu` is an `IMenuSource` produced by `MenuBuilder.Build()`.
**Presentation**: Always-visible horizontal menu with custom `MenuItem` rendering and zero-delay hover switching.
**Status**: 🚧 **Template specification authored; implementation pending**

### 3. Custom MenuFlyout (🚧 Template Design In Progress)

**What it is**: A popup container that reuses the same templated `MenuItem` visuals inside a `FlyoutBase` surface, backed by `MenuSource`.
**Usage**: Planned API `<controls:MenuFlyout MenuSource="{x:Bind ViewModel.ContextMenu}" />` with the view model exposing an `IMenuSource` for contextual scenarios.
**Presentation**: Popup overlay with cascading submenus and shared selection logic sourced from `MenuServices`.
**Status**: 🚧 **Template specification authored; implementation pending**

### 4. Custom ExpandableMenuBar (🛠️ Design Authored, Build Pending)

**What it is**: Collapsible menu bar with a hamburger entry point that swaps space with the full menu.
**Usage**: Target API `<controls:ExpandableMenuBar MenuSource="{x:Bind ViewModel.MainMenu}" IsExpanded="{x:Bind ViewModel.IsMenuExpanded, Mode=TwoWay}" />` backed by the shared `IMenuSource` instance.
**Presentation**: Space-swapping hamburger ↔ menu bar using `MenuItem` rendering and hover-driven navigation.
**Status**: 🛠️ **Design documented below; implementation not yet started**

## Table of Contents

- [1. ExpandableMenuBar User Experience Model](#1-expandablemenubar-user-experience-model)
  - [1.1 System Entry - The Lightning Start](#11-system-entry---the-lightning-start)
  - [1.2 Lightning-Fast Hover Navigation - The Core Experience](#12-lightning-fast-hover-navigation)
  - [1.3 Command Execution - The Final Click](#13-command-execution---the-final-click)
  - [1.4 Smart Dismissal - Getting Out of the Way](#14-smart-dismissal---getting-out-of-the-way)
  - [1.5 Power User Keyboard Navigation](#15-power-user-keyboard-navigation)
  - [1.6 User Journey Examples](#16-user-journey-examples)
  - [1.7 Visual Design Requirements](#17-visual-design-requirements)
  - [1.8 Accessibility Support](#18-accessibility-support)
- [2. ExpandableMenuBar Technical Architecture](#2-expandablemenubar-technical-architecture)
  - [2.1 System Overview](#21-system-overview)
  - [2.2 Core Components](#22-core-components)
  - [2.3 Data Model - Optimized for Speed](#23-data-model---optimized-for-speed)
- [3. Implementation Status & Roadmap](#3-implementation-status--roadmap)
- [4. Design Decisions & Rationale](#4-design-decisions--rationale)
- [5. Success Metrics](#5-success-metrics)

## Custom Menu System Specifications

### Core Principle: Single Data Model, Multiple Presentations

**MenuBuilder as Single Source of Truth**: All menu types use the same `MenuBuilder` instance with consistent custom rendering.

### Custom Menu Item Presentation Standard

**Your Custom Column Layout**:

```text
[Icon] [Text]                    [Accelerator] [State/Arrow]
💾     Save                      Ctrl+S
       New Project               Ctrl+N
       ————————————————————————————————————————————————————
💾     Auto Save                              ✓     ← Icon + right-side checkmark
✓      Show Line Numbers                            ← Left-side checkmark (no icon)
💾     Wrap                     Shift+Alt+W  ✓     ← Icon + accelerator + checkmark
✓      God Mode                 Ctrl+G             ← Checkmark + accelerator
       More stuff                             >     ← Submenu arrow
```

**Layout Rules**:

- **Column 1**: Semantic icon (16×16px) or checkmark (if no icon)
- **Column 2**: Menu item text (left-aligned)
- **Column 3**: Keyboard accelerator (right-aligned)
- **Column 4**: Selection state (✓) or submenu arrow (>) - right-aligned
- **Checkmark placement**: Left side if no icon, right side if icon present
- **Consistent alignment**: All ✓ and > indicators align on right edge

### 1. Custom MenuBar (🚧 Template in design)

**Purpose**: A WinUI templated control that renders the root menu hierarchy as an always-visible horizontal bar while delegating lookup and group behavior to `MenuServices`.
**Primary contract**: `MenuSource` dependency property of type `IMenuSource`. The control consumes `MenuSource.Items` for composition and forwards interactions to `MenuSource.Services`.

#### Control blueprint

- **Type**: `public sealed class MenuBar : Control`
- **Template parts**
  - `PART_RootItemsRepeater` (`ItemsRepeater` or `ListViewBase`)—lays out root items horizontally.
  - `PART_SubmenuPopupLayer` (`Popup`/`FlyoutPresenter`)—hosts cascading submenu surfaces.
  - `PART_FocusTracker` helper—tracks keyboard focus and restores location after dismissal.
- **Dependency properties**
  - `MenuSource` (`IMenuSource`)—required.
  - `IsSubmenuOpen` (`bool`)—optional visual-state flag for templates.
  - `OpenRootIndex` (`int`)—exposes the active root for telemetry or two-way binding.
- **Visual states**
  - `RootClosed` / `RootOpen`
  - `SubmenuIdle` / `SubmenuActive`
  - `PointerNavigation` / `KeyboardNavigation`

#### Interaction contract

- **Pointer**: Hovering a root item sets `OpenRootIndex`, materializes its children in `PART_SubmenuPopupLayer`, and keeps the bar hot-tracked with no delay.
- **Submenu surfacing**: The popup layer hosts the shared `MenuColumnPresenter` used by `MenuFlyout`, so every hover or tap on a root item renders the exact same column surface as a flyout would. The menu bar does not spin up a separate `FlyoutBase`; it reuses the flyout presenter template for in-place submenus.
- **Keyboard**: Arrow keys travel across roots and down column items; `Enter`/`Space` invokes leaf items and defers group toggling to `MenuServices.HandleGroupSelection`.
- **Selection**: Checkable and radio items always route through `MenuServices.HandleGroupSelection` so that only the relevant group members flip state.
- **Lifecycle**: The control listens to collection change events on `MenuSource.Items`, and because the builder reuses its `MenuSource`, lookup snapshots stay valid without reallocation.

#### Usage

```csharp
// ViewModel – compose once and expose IMenuSource
public IMenuSource MainMenu { get; } = new MenuBuilder()
        .AddSubmenu("&File", file => file
                .AddMenuItem("&New Project", command: NewCommand, icon: new SymbolIconSource { Symbol = Symbol.Add }, acceleratorText: "Ctrl+N")
                .AddMenuItem("&Save", command: SaveCommand, icon: new SymbolIconSource { Symbol = Symbol.Save }, acceleratorText: "Ctrl+S")
                .AddSeparator()
                .AddCheckableMenuItem("&Auto Save", isChecked: true, icon: new FontIconSource { Glyph = "\uE74E" }))
        .AddSubmenu("&Edit", edit => edit
                .AddRadioMenuItem("&Light", "theme", isChecked: true)
                .AddRadioMenuItem("&Dark", "theme"))
        .Build();

// XAML – bind the templated control to the reusable source
// <controls:MenuBar MenuSource="{x:Bind ViewModel.MainMenu}" />
```

### 2. Custom MenuFlyout (🚧 Template in design)

**Purpose**: A popup container that reuses the `MenuItem` four-column layout while honoring the same data-first contracts as the `MenuBar`.
**Primary contract**: `MenuSource` dependency property of type `IMenuSource`, consumed by a custom presenter that supports cascading submenus.

#### Control blueprint

- **Type**: `public sealed class MenuFlyout : FlyoutBase`
- **Template parts**
  - `PART_Presenter`—a templated root derived from `FlyoutPresenter` that hosts the layout grid.
  - `PART_ItemsRepeater`—renders top-level items vertically.
  - `PART_SubmenuPopupLayer`—manages nested submenus sharing the same template.
- **Dependency properties**
  - `MenuSource` (`IMenuSource`)—required.
  - `MaxColumnHeight` (`double`)—optional to clamp tall menus with internal scrolling.
- **Visual states**
  - `Closed` / `Open`
  - `SubmenuIdle` / `SubmenuActive`
- **Dismissal**: Closes automatically after executing a leaf command, when `MenuServices.HandleGroupSelection` toggles a radio option, or when focus leaves the flyout.

#### Interaction contract

- `ShowAt` binds `MenuSource.Items` to the presenter, precomputes the lookup dictionary via `MenuServices.GetLookup()`, and primes keyboard focus on the first enabled item.
- Hovering grouped items delegates to `MenuServices.HandleGroupSelection`, ensuring consistent single-selection semantics between flyout and menubar.
- Keyboard navigation mirrors standard flyout behavior: arrows move, `Enter` executes, `Escape` dismisses.
- MenuBar roots dispatch to the same presenter pipeline: when a root opens, its `MenuSource.Items` are shown through the `MenuFlyout` presenter so pointer, tap, and keyboard flows remain identical whether content started life in a bar or a standalone flyout.

#### Shared interaction behaviors

- **Tap/Click**
  - *MenuBar*: Clicking a root opens its submenu using the shared flyout presenter; clicking a leaf executes the command and collapses the chain.
  - *MenuFlyout*: Clicking anywhere on the surface follows the same rule—leaf executes and dismisses, parents just expand via the shared presenter.
- **Hover/Pointer**
  - *MenuBar*: Hovering a root or submenu item hot-tracks immediately, opening the same presenter columns that the flyout uses.
  - *MenuFlyout*: Hovering items mirrors the bar, expanding children instantly with the identical presenter and corridor logic.
- **Keyboard**
  - *MenuBar*: F10/Alt enters the bar, arrows move across roots and through items, `Enter`/`Space` invokes leaves, `Escape` collapses the current presenter stack.
  - *MenuFlyout*: When shown, it owns focus; arrows traverse items and levels, `Enter`/`Space` invokes, `Escape` dismisses the flyout just like collapsing from the bar.

#### Usage

```csharp
private readonly IMenuSource contextMenu = new MenuBuilder()
        .AddMenuItem("Cut", command: CutCommand, icon: new SymbolIconSource { Symbol = Symbol.Cut }, acceleratorText: "Ctrl+X")
        .AddMenuItem("Copy", command: CopyCommand, icon: new SymbolIconSource { Symbol = Symbol.Copy }, acceleratorText: "Ctrl+C")
        .AddMenuItem("Paste", command: PasteCommand, icon: new ImageIconSource { UriSource = new Uri("ms-appx:///Assets/paste-icon.png") }, acceleratorText: "Ctrl+V")
        .AddSeparator()
        .AddMenuItem("Properties", command: PropertiesCommand, icon: new FontIconSource { Glyph = "\uE713", FontFamily = new FontFamily("Segoe MDL2 Assets") })
        .Build();

// Usage in code-behind
var flyout = new controls.MenuFlyout { MenuSource = this.contextMenu };
flyout.ShowAt(this.ContextMenuTarget);
```

### 3. Custom ExpandableMenuBar

**Purpose**: Space-efficient hamburger that expands to full menu bar in title bar
**XAML Usage**: Planned API `<controls:ExpandableMenuBar MenuItems="{x:Bind ViewModel.MainMenuItems}" IsExpanded="{x:Bind ViewModel.IsMenuExpanded, Mode=TwoWay}" />`
**Behavior**: Click hamburger → space-swapping transformation to menu bar

**Key UX Principle**: **SPACE SWAPPING** - not popup overlay!

When hamburger clicked:

- Hamburger disappears from title bar
- Full menu bar appears in same title bar space
- Click elsewhere → menu bar collapses back to hamburger
- Same custom column layout as MenuBar when expanded

**Space-Swapping Flow**:

```text
Title Bar Area:
┌─────────────────────────────────────┐
│ [MyApp]           [☰]    [- □ ×]    │  ← Default: Hamburger in title bar
└─────────────────────────────────────┘
                     ↓ Click
┌─────────────────────────────────────┐
│ [MyApp] [File▼][Edit][View] [- □ ×] │  ← MenuBar replaces hamburger
└─────────┬───────────────────────────┘
          ├─ New     ↑ First menu auto-opens
          ├─ Open
          ├─ Save
          └─ Exit
                     ↓ Select/Escape/Click outside
┌─────────────────────────────────────┐
│ [MyApp]           [☰]    [- □ ×]    │  ← Back to hamburger
└─────────────────────────────────────┘
```

**Implementation (planned API surface)**:

```csharp
// Compose once – same MenuBuilder powering MenuBar and MenuFlyout
public IMenuSource MainMenu { get; } = new MenuBuilder()
    .AddSubmenu("File", file => file
        .AddMenuItem("New", command: NewCommand)
        .AddMenuItem("Open", command: OpenCommand)
        .AddMenuItem("Save", command: SaveCommand)
        .AddSeparator()
        .AddMenuItem("Exit", command: ExitCommand))
    .AddSubmenu("Edit", edit => edit
        .AddMenuItem("Undo", command: UndoCommand)
        .AddMenuItem("Redo", command: RedoCommand))
    .AddSubmenu("View", view => view
        .AddCheckableMenuItem("Status Bar", isChecked: true)
        .AddCheckableMenuItem("Output", isChecked: false))
    .AddSubmenu("Help", help => help
        .AddMenuItem("About", command: AboutCommand))
    .Build();

public bool IsMenuExpanded { get; set; } = false;

// XAML – templated control toggles hamburger ↔ menubar using IMenuSource
// <controls:ExpandableMenuBar
//     MenuSource="{x:Bind ViewModel.MainMenu}"
//     IsExpanded="{x:Bind ViewModel.IsMenuExpanded, Mode=TwoWay}" />
```

### 4. Custom MenuItem Control - The Foundation

**Purpose**: Individual menu item rendering with your custom column layout
**XAML Usage**: Used internally by MenuBar, MenuFlyout, and ExpandableMenuBar
**Key Feature**: This is the **CORE CONTROL** that renders your custom presentation design

**Column Layout Requirements**:

```text
┌─────────────────────────────────────────────────────────────┐
│ [Icon] │ Menu Text              │ Accelerator │ [State] │    │
│   🗂    │ New Project            │   Ctrl+N    │    ►    │    │
│   💾   │ Save                   │   Ctrl+S    │         │    │
│        │ ─────────────────────── │             │         │    │ ← Separator
│   ☑    │ Auto Save              │             │    ✓    │    │ ← Checkable
│   ☑    │ Light Theme            │             │    ✓    │    │ ← Grouped (radio-like but uses checkmark)
└─────────────────────────────────────────────────────────────┘
│       │                       │             │         │
└─ Icon │                       │             │         └─ State
  16px   │                       │             └─ Selection/Arrow
         │                       └─ Accelerator (right-aligned)
         └─ Text (left-aligned, expandable)
```

**Custom MenuItem Control Implementation**:

```csharp
// Custom control that renders MenuItemData data with your layout
public class CustomMenuItem : ContentControl
{
    public MenuItemData MenuItem { get; set; }

    // Template provides 4-column layout: Icon|Text|Accelerator|State
    // - Icon column: 16px width, renders IconSource (SymbolIcon, FontIcon, ImageIcon, etc.)
    // - Text column: Expandable, left-aligned
    // - Accelerator column: Fixed width, right-aligned
    // - State column: 16px width, centered (checkmarks, arrows)
}

// XAML Template for CustomMenuItem:
// <Grid>
//   <Grid.ColumnDefinitions>
//     <ColumnDefinition Width="16"/>        <!-- Icon -->
//     <ColumnDefinition Width="*"/>         <!-- Text -->
//     <ColumnDefinition Width="Auto"/>      <!-- Accelerator -->
//     <ColumnDefinition Width="16"/>        <!-- State -->
//   </Grid.ColumnDefinitions>
//
//   <!-- Icon Column: Renders any IconSource type -->
//   <IconSourceElement Grid.Column="0" IconSource="{Binding MenuItemData.Icon}" />
//
//   <!-- Text Column -->
//   <TextBlock Grid.Column="1" Text="{Binding MenuItemData.Text}" />
//
//   <!-- Accelerator Column -->
//   <TextBlock Grid.Column="2" Text="{Binding MenuItemData.AcceleratorText}" />
//
//   <!-- State Column: Checkmarks, arrows -->
//   <TextBlock Grid.Column="3" Text="✓" Visibility="{Binding MenuItemData.IsChecked}" />
// </Grid>
```

**Why This Is Critical**: All three menu controls (MenuBar, MenuFlyout, ExpandableMenuBar) use CustomMenuItem internally to render individual menu items. This ensures consistent presentation across all menu types.

## Custom Menu System Comparison

| Feature | CustomMenuItem | Custom MenuBar | Custom MenuFlyout | Custom ExpandableMenuBar |
|---------|---------|---------|------------|-------------------|
| **Status** | ✅ **Implemented** | 🔄 **Need Custom Control** | 🔄 **Need Custom Control** | 🔄 **Need Custom Control** |
| **Purpose** | Individual item rendering | Menu container | Popup container | Space-swapping container |
| **Data Source** | Single MenuItemData | MenuBuilder | MenuBuilder | MenuBuilder |
| **Presentation** | Icon\|Text\|Accel\|State columns | Horizontal menu using CustomMenuItem | Popup using CustomMenuItem | Hamburger ↔ menubar using CustomMenuItem |
| **Use Case** | Foundation for all menus | Always-visible app menus | Context/popup menus | Title bar space-efficient menus |
| **Appearance** | Individual menu item | Top of window menu bar | Popup on demand | Hamburger button + space-swapping |
| **Activation** | Hover/click | Click menu root | Right-click or button | Click hamburger (☰) button |
| **Dismissal** | N/A - part of container | Click elsewhere | Select item or click outside | Select item, Escape, or click outside |
| **Implementation** | `DroidNet.Controls.MenuItem` custom control with full visual state suite | Interim WinUI `MenuBar` host; custom container pending | Interim WinUI `MenuFlyout` host; custom container pending | Custom control not yet started (design authored) |

## Which Menu Should You Use?

- **Traditional desktop app with always-visible menus?** → Use **MenuBar**
- **Right-click context menus or button dropdowns?** → Use **MenuFlyout**
- **Want space-saving collapsible menu bar?** → **ExpandableMenuBar** (once implemented)

All three use the same `MenuBuilder` API and `MenuItemData` data model for consistency.

---

## ExpandableMenuBar Design Specification

**Current Status**: ❌ Needs Implementation (currently called MenuSystemHost - requires renaming)

### Design Philosophy

This menu system is designed for **power users who value speed over discovery**.
Every interaction is optimized for the user who knows exactly what they want and
gets there in the fewest possible actions, while providing space-saving benefits.

**Core Principles:**

1. **One-Click Rule**: Only the expand button and final command require clicks
2. **Instant Response**: All navigation is hover-driven with zero perceived delay
3. **Predictable Behavior**: Consistent patterns that become muscle memory
4. **No Wasted Motion**: Every pointer movement serves a purpose
5. **Space Efficient**: Collapsible design saves screen real estate

---

## 1. ExpandableMenuBar User Experience Model

### 1.1 System Entry - The Lightning Start

**User Goal**: Get into the ExpandableMenuBar system instantly and see relevant options immediately.

**Interaction Flow**:

```text
[User clicks ☰] → [Button vanishes] → [MenuBar appears in same space] → [Ready to navigate]
                    instantly        instantly                       immediate
```

**What Happens:**

- Single click on hamburger button (☰) - this is the ONLY required click for navigation
- Hamburger button disappears instantly (no animation delay)
- MenuBar appears in the same title bar location with:
  - Horizontal menu bar (File, Edit, View, Tools, Help...)
  - First menu (File) automatically expanded showing its dropdown
  - Visual focus on "File" menu
  - Dropdown positioned optimally (typically below MenuBar)

**Critical Requirements:**

- **Immediate response** between click and menu appearance
- **Auto-expansion** of first menu eliminates second click
- **Instant visual feedback** - user immediately sees actionable items

### 1.2 Lightning-Fast Hover Navigation

**User Goal**: Move to any menu item using only pointer movement, with zero mental overhead.

**Root Menu Switching** (Horizontal Movement):

```text
File    Edit    View    Tools    Help
 ▲       ▲       ▲       ▲        ▲
 │       │       │       │        │
 └── Hover here switches instantly ──┘
```

**Behavior:**

- **Instant switching**: Hovering "Edit" while "File" is open immediately shows Edit submenu
- **No hover delay**: Response is immediate without timeout
- **Visual continuity**: Root highlight moves smoothly, submenu content swaps instantly
- **Predictable bounds**: Only the text area of each root responds to hover

**Cascading Submenus** (Vertical Navigation):

```text
File
├─ New Project     →  [Recent Projects]
├─ Open File          ├─ Project Alpha
├─ Recent Files    →  ├─ Project Beta
├─ Save               └─ Project Gamma
└─ Exit
```

**Behavior:**

- **Immediate expansion**: Items with children (►) expand on hover without delay
- **Smart positioning**: Child menus appear to the right, or left if no space
- **Auto-cleanup**: Moving to sibling items closes previous child menus
- **Depth unlimited**: Supports any nesting level user needs

**Critical Performance Requirements:**

- **Instantaneous response**: Hover changes must feel immediate
- **No accidental triggers**: Requires deliberate hover over item text/icon area
- **Smooth transitions**: Visual changes should not cause flicker or jumpiness

### 1.3 Command Execution - The Final Click

**User Goal**: Execute the desired command with a single, confident click.

**Leaf Item Activation** (Items with no children):

```text
User Journey: Hover → Hover → Click → Done
             File    Save    [Click]   Menu closes, command executes
```

**Behavior:**

- **Single click execution**: Click immediately runs the command
- **Instant dismissal**: Menu system vanishes the moment command fires
- **No confirmation**: Assumes user intent is deliberate
- **Command feedback**: Application handles success/error feedback

**Parent Item Behavior** (Items with children):

**Rule**: Parent items are **containers only** - they never execute commands on click.

```text
File
├─ Recent Files    ← Click here does NOTHING except expand (if not already open)
│  ├─ Document.txt ← Click here EXECUTES "open Document.txt"
│  └─ Image.png    ← Click here EXECUTES "open Image.png"
└─ Exit            ← Click here EXECUTES "exit application"
```

**Why This Design**:

- **Prevents accidents**: No accidental command execution when navigating
- **Clear intent**: Only leaf items represent actual actions
- **Consistent behavior**: User always knows what a click will do

### 1.4 Smart Dismissal - Getting Out of the Way

**User Goal**: Exit the ExpandableMenuBar quickly when done, or when focus shifts.

**Automatic Dismissal** (No user action required):

```text
✓ Command executed        → MenuBar collapses, ☰ button reappears in same space
✓ Clicked outside menu    → MenuBar collapses, ☰ button reappears in same space
✓ Window lost focus       → MenuBar collapses, ☰ button reappears in same space
```

**Manual Dismissal** (User-initiated exit):

```text
✓ Escape key             → MenuBar collapses, ☰ button reappears (gets focus)
✓ Alt key (double-tap)   → MenuBar collapses (optional feature)
```

**What Stays Open**:

- **Hover navigation**: Moving between menu items never closes the system
- **Brief mouse exit**: Accidentally moving outside menu area has small grace period
- **Keyboard focus**: Using arrow keys, Tab, etc. keeps menu open

**Critical Behavior**:

- **Instant response**: All dismissal triggers work immediately
- **Predictable**: User always knows how to exit (MenuBar collapses, ☰ reappears)
- **Space efficient**: Same title bar real estate used for both states
- **Forgiving**: Accidental actions don't cause data loss

### 1.5 Power User Keyboard Navigation

**User Goal**: Navigate menus without using mouse, with standard keyboard shortcuts.

**Essential Keys** (Core navigation):

| Key | Action | Speed Benefit |
|-----|--------|---------------|
| **Arrow Keys** | Navigate items/roots | Precise movement |
| **Enter** | Execute command or expand | Direct activation |
| **Escape** | Close submenu or exit | Quick escape route |
| **Alt** | Show mnemonics | Expert shortcuts |

**Navigation Patterns**:

```text
Root Level:     Left/Right switches roots (File→Edit→View)
Menu Level:     Up/Down moves through items
Submenu:        Right opens children, Left closes current level
```

**Mnemonic Support** (Alt + Letter):

```text
Alt+F → File menu    Alt+E → Edit menu    Alt+V → View menu
Alt+N → New item     Alt+S → Save item    Alt+X → Exit
```

**Advanced Features**:

- **Type-ahead**: Type "sa" to jump to "Save" item
- **Home/End**: Jump to first/last item in current menu
- **F10**: Alternative menu activation (Windows standard)

---

### 1.6 User Journey Examples

### Scenario 1: Expert User Executing Known Command

**Goal**: Execute File → Export → PDF quickly

**Flow**:

```text
User Action           System Response                 Visual State
Click expand (☰)     Menu appears                    File menu open
Hover "Export"       Export submenu opens            File→Export visible
Click "PDF"          Command executes, menu closes   Back to normal UI
```

**Key Success**: Three simple actions accomplish the task.

### Scenario 2: User Exploring Options

**Goal**: Browse available commands

**Flow**:

```text
User Action           System Response                 Visual State
Click expand (☰)     Menu appears                    File menu open
Hover "Edit"         Edit submenu opens              Edit menu visible
Hover "Transform"    Transform submenu opens         Edit→Transform visible
Hover "View"         View submenu opens              View menu visible
Click "Zoom In"      Command executes                Action performed
```

**Key Success**: Exploration is effortless through hover navigation.

### Scenario 3: Accidental Recovery

**Goal**: Exit menu quickly when opened by mistake

**Flow**:

```text
User Action           System Response                 Visual State
Click expand (☰)     Menu appears                    File menu open
Press Escape         Menu closes instantly           Back to normal UI
```

**Key Success**: Single key press provides immediate escape.

### 1.7 Visual Design Requirements

**Root Menu Bar**:

- Horizontal, compact buttons with clear text labels
- Active root menu has distinct highlight (background or accent color)
- Consistent spacing and alignment across all root items

**Submenu Panels**:

- Light elevation with subtle borders for depth perception
- Items display icons, text, and keyboard shortcuts when applicable
- Hover states provide clear visual feedback
- Disabled items use reduced opacity and no hover response

**Layout Behavior**:

- Submenus align to parent items when possible
- Smart positioning flips left/right based on available space
- Long menus scroll internally rather than extending beyond screen
- Consistent column widths within each menu level

### 1.8 Accessibility Support

**Screen Reader Compatibility**:

- Proper ARIA roles: menubar, menu, menuitem, menuitemcheckbox
- State announcements for menu opening/closing and item selection
- Full keyboard navigation support with standard patterns

**Keyboard Access**:

- All functionality available without mouse
- Standard Windows menu keyboard shortcuts (Alt+key for mnemonics)
- Visible focus indicators that meet accessibility guidelines

---

- Each submenu column aligned top to the hovered parent item’s vertical center
  (standard) OR simply top-aligned (simpler initial implementation).
- If column would overflow right edge of window, the cascade flips horizontally
  (draws to the left) while sustaining pointer corridor logic.

---

## 2. ExpandableMenuBar Technical Architecture

### Built for Speed and Reliability

## 2.1 System Overview

The ExpandableMenuBar is designed as a **single, coordinated control** rather than
multiple independent flyouts. This ensures instant hover switching and
predictable behavior while providing space-saving collapsible functionality.

**Core Architecture**:

```text
ExpandableMenuBar - Space-Swapping Design:

Default State (Hamburger Visible):
┌─────────────────────────────────────────────────────────────────┐
│ ExpandableMenuBar (UserControl)                                │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ [☰] Button (Visible)    MenuBar (Collapsed/Hidden)         │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘

Expanded State (MenuBar Visible):
┌─────────────────────────────────────────────────────────────────┐
│ ExpandableMenuBar (UserControl)                                │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ [☰] Button (Collapsed/Hidden)    MenuBar (Visible)         │ │
│ │                                  [File▼][Edit][View][Help] │ │
│ │                                  └── Auto-expanded dropdown │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## 2.2 Core Components

### ExpandableMenuBar - WinUI Control

**Responsibility**: Renders MenuItemData data model, handles hamburger button, and manages expand/collapse behavior

```csharp
[TemplatePart(Name = "PART_ExpandButton", Type = typeof(Button))]
[TemplatePart(Name = "PART_MenuBar", Type = typeof(MenuBar))]
public sealed class ExpandableMenuBar : Control
{
    // DependencyProperties for data binding
    public static readonly DependencyProperty ItemsProperty =
        DependencyProperty.Register(nameof(Items), typeof(ObservableCollection<MenuItemData>),
            typeof(ExpandableMenuBar), new PropertyMetadata(null, OnItemsChanged));

    public static readonly DependencyProperty IsOpenProperty =
        DependencyProperty.Register(nameof(IsOpen), typeof(bool),
            typeof(ExpandableMenuBar), new PropertyMetadata(false, OnIsOpenChanged));

    // Routed Events following WinUI patterns
    public static readonly RoutedEvent ItemInvokedEvent =
        EventManager.RegisterRoutedEvent(nameof(ItemInvoked), RoutingStrategy.Bubble,
            typeof(TypedEventHandler<ExpandableMenuBar, MenuItemInvokedEventArgs>), typeof(ExpandableMenuBar));

    // CLR Properties
    public ObservableCollection<MenuItemData> Items
    {
        get => (ObservableCollection<MenuItemData>)GetValue(ItemsProperty);
        set => SetValue(ItemsProperty, value);
    }

    public bool IsOpen
    {
        get => (bool)GetValue(IsOpenProperty);
        set => SetValue(IsOpenProperty, value);
    }

    // Events
    public event TypedEventHandler<ExpandableMenuBar, MenuItemInvokedEventArgs> ItemInvoked
    {
        add => AddHandler(ItemInvokedEvent, value);
        remove => RemoveHandler(ItemInvokedEvent, value);
    }

    // Control lifecycle
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        // Initialize template parts and wire up event handlers
        // Wire up hamburger button click event to toggle visibility
        // Set up MenuBar from Items collection
    }

    // Public API methods
    public void Expand() => IsOpen = true;    // Show MenuBar, hide hamburger
    public void Collapse() => IsOpen = false; // Show hamburger, hide MenuBar
}
```

### MenuController

**Responsibility**: State management and input coordination

```csharp
internal sealed class MenuController
{
    // State management
    public int ActiveRootIndex { get; private set; }
    public IReadOnlyList<MenuColumnState> OpenColumns { get; private set; }

    // Core operations
    public void SetActiveRoot(int rootIndex);
    public void OpenSubmenu(string parentItemId, ObservableCollection<MenuItemData> items);
    public void ExecuteCommand(MenuItemData item);

    // Input handling
    public void HandlePointerEntered(FrameworkElement target);
    public void HandleKeyDown(KeyEventArgs args);
}
```

### MenuHost

**Responsibility**: Visual layout and rendering coordination

- Manages the popup container and overall layout
- Positions root menu bar and cascading columns
- Handles window resize and boundary detection
- Coordinates visual transitions between states

### MenuColumn

**Responsibility**: Renders individual menu levels

- Displays menu items with icons, text, accelerators
- Handles item hover states and visual feedback
- Manages scrolling for long item lists
- Raises events for item interaction

## 2.3 Data Model - Optimized for Speed

### MenuItemData - Data Model

**The foundation of the entire system** - a data model class that represents menu structure and commands:

```csharp
public sealed class MenuItemData : ObservableObject
{
    // Core identity and display
    public string Text { get; set; } = string.Empty;
    public IconSource? Icon { get; set; }              // Supports all icon types: SymbolIcon, FontIcon, ImageIcon, BitmapIcon
    public char? Mnemonic { get; set; }                // Alt+F keyboard shortcut
    public string? AcceleratorText { get; set; }       // "Ctrl+S" display text

    // Command behavior
    public ICommand? Command { get; set; }
    public bool IsEnabled { get; set; } = true;
    public bool IsSeparator { get; set; }

    // Selection state (checkable/toggleable items)
    public bool IsChecked { get; set; }                // Persistent toggle state for checkable items
    public bool IsActive { get; set; }                 // Hot-tracked/keyboard active state (transient)
    public bool IsCheckable { get; set; }              // Can be toggled on/off individually
    public string? RadioGroupId { get; set; }          // Grouped items (only one selected per group)

    // Hierarchy
    public IEnumerable<MenuItemData> SubItems { get; set; } = [];

    // Computed properties
    public bool HasChildren => SubItems.Any();
    public bool IsLeafItem => !HasChildren;
    public bool HasSelectionState => IsCheckable || !string.IsNullOrEmpty(RadioGroupId);
    public string Id => Text.Replace('.', '_').ToUpperInvariant();
}
```

### IconSource Support - All Icon Types

**CRITICAL**: The `Icon` property uses `IconSource?` to support all WinUI icon types:

**SymbolIconSource** (Built-in Segoe MDL2 symbols):

```csharp
Icon = new SymbolIconSource { Symbol = Symbol.Save }
Icon = new SymbolIconSource { Symbol = Symbol.Copy }
Icon = new SymbolIconSource { Symbol = Symbol.Add }
```

**FontIconSource** (Custom font glyphs):

```csharp
Icon = new FontIconSource
{
    Glyph = "\uE74E",
    FontFamily = new FontFamily("Segoe MDL2 Assets")
}
Icon = new FontIconSource
{
    Glyph = "\uF0C7",
    FontFamily = new FontFamily("Font Awesome")
}
```

**ImageIconSource** (PNG, SVG, etc.):

```csharp
Icon = new ImageIconSource
{
    UriSource = new Uri("ms-appx:///Assets/custom-icon.png")
}
Icon = new ImageIconSource
{
    UriSource = new Uri("ms-appx:///Assets/vector-icon.svg")
}
```

**BitmapIconSource** (Bitmap images):

```csharp
Icon = new BitmapIconSource
{
    UriSource = new Uri("ms-appx:///Assets/bitmap-icon.png"),
    ShowAsMonochrome = false
}
```

**Why IconSource Matters**: Ensures your custom MenuItem control can render any icon type consistently within the Icon column of your custom layout.

### Group Selection Logic (Code-Behind)

**RadioGroupId Behavior**: Items with the same RadioGroupId act as mutually exclusive options, but **visually use checkmarks, NOT radio buttons**.

**Implementation Pattern**:

```csharp
// When user selects a grouped item:
private void OnMenuItemSelected(MenuItemData selectedItem)
{
    if (!string.IsNullOrEmpty(selectedItem.RadioGroupId))
    {
        // Find all items in the same group and unselect them
        var groupItems = menuBuilder.AllItems
            .Where(item => item.RadioGroupId == selectedItem.RadioGroupId);

        foreach (var item in groupItems)
        {
            item.IsChecked = false;  // Clear all checkmarks in group
        }

        selectedItem.IsChecked = true;  // Set checkmark on selected item only
    }
    else if (selectedItem.IsCheckable)
    {
        // Individual checkable item - just toggle
        selectedItem.IsChecked = !selectedItem.IsChecked;
    }
}
```

**Visual Result**: All grouped items show checkmark (✓) when selected - user sees consistent checkmark behavior, code enforces single-selection logic.

### Icon vs Selection State UX Pattern

**Crystal Clear Design Principle**: Semantic icons and selection indicators serve different purposes and use different visual channels:

**Semantic Icons** (Left Side):

- **Purpose**: Show what the command represents (Save, Copy, Print, etc.)
- **Position**: Left side of menu item, 16×16px standard size
- **Behavior**: Always displayed when set, regardless of selection state
- **Examples**: 💾 Save, 📄 Copy, 🖨️ Print

**Selection Indicators** (Right Side):

- **Purpose**: Show current toggle/selection state
- **Position**: Right side State column in custom layout
- **Behavior**: Checkmark (✓) appears/disappears based on IsChecked
- **Types**:
  - ✅ **Checkable items**: Individual on/off toggle (IsCheckable = true)
  - ✅ **Grouped items**: Only one selected per group (RadioGroupId set, code-behind enforces single selection)

**Visual Consistency**: Both checkable and grouped items use the same checkmark (✓) - NO radio buttons. Group behavior (single selection) is handled in code-behind logic.

**Visual Layout**:

```text
[Icon] Menu Item Text                   [✓] ← Framework checkmark
[💾]  Save File                         ✓ ← Icon + checkmark both visible
[📄]  Copy Selection                      ← Icon visible, no checkmark
  ✓   Auto Save                           ← No icon, checkmark only
```

**Implementation Details**:

- CustomMenuItem renders State column based on `HasSelectionState` property
- Checkmarks (✓) appear in State column when `IsChecked = true`
- Icons and checkmarks can coexist without conflict
- **Group Logic**: Code-behind handles RadioGroupId - when item selected, unselect others in same group

**Example Usage**:

```csharp
// Command item with semantic icon (no selection state)
new MenuItemData
{
    Text = "Save File",
    Icon = new SymbolIcon(Symbol.Save),
    Command = SaveCommand
}

// Checkable item with icon (both icon and checkmark visible)
new MenuItemData
{
    Text = "Auto Save",
    Icon = new SymbolIcon(Symbol.Save),
    IsCheckable = true,
    IsCheckable = true,
    IsChecked = true,   // Shows checkmark
    Command = ToggleAutoSaveCommand
}

// Grouped item (mutually exclusive selection - uses checkmark, NOT radio button)
new MenuItemData
{
    Text = "Light Theme",
    RadioGroupId = "Theme",
    IsChecked = true,   // Shows checkmark ✓ (code-behind ensures only one per group)
    Command = SetThemeCommand
}
```

### MenuColumnState

**Tracks what's currently visible** - enables instant state restoration:

```csharp
internal sealed class MenuColumnState
{
    public string? ParentItemId { get; }                    // null for root
    public ObservableCollection<MenuItemData> Items { get; }   // Observable for binding
    public int FocusedIndex { get; set; } = -1;            // -1 = no focus
    public Rect AnchorBounds { get; set; }                 // Position relative to parent
}
```

### Key Design Decisions

**Speed Optimizations**:

- **Immutable items**: No change notifications overhead during navigation
- **Pre-computed properties**: `HasChildren`, `IsLeafItem` calculated once
- **Flat command lookup**: Direct `ICommand` reference, no string-based routing
- **Minimal state**: Only track what's needed for current display

**Memory Efficiency**:

- **Shared item references**: Same `MenuItemData` used across multiple contexts
- **Lazy column creation**: Visual containers created only when needed
- **Event pooling**: Reuse event args objects to minimize allocations

### Core Data Components

#### MenuBuilder

`MenuBuilder` is the fluent factory for hierarchical `MenuItemData` graphs. Its public surface intentionally exposes only the fluent adders and a single `Build()` method that returns an `IMenuSource`.

- Generates deterministic identifiers by normalizing text, enforcing uniqueness per scope, and stamping IDs top-down each time `Build()` is called.
- Uses private traversal helpers to realize subtrees, enumerate items, and reapply identifiers, keeping the fluent API focused on intent.
- Keeps an internal dirty flag so changes made after a build automatically refresh the lookup the next time services are accessed.

```csharp
var builder = new MenuBuilder()
    .AddSubmenu("File", file => file
        .AddMenuItem("New", command: NewCommand)
        .AddMenuItem("Open", command: OpenCommand)
        .AddSeparator()
        .AddMenuItem("Exit", command: ExitCommand))
    .AddSubmenu("View", view => view
        .AddRadioMenuItem("Light", "theme", isChecked: true)
        .AddRadioMenuItem("Dark", "theme"));

IMenuSource menu = builder.Build();
```

#### MenuSource and IMenuSource

`Build()` returns an `IMenuSource` that pairs the realized item collection with the associated `MenuServices` instance:

- `Items` – a reusable `ObservableCollection<MenuItemData>` surfaced by the builder. Subsequent `Build()` calls reuse the same collection instance, so bindings remain intact.
- `Services` – the cached `MenuServices` object that exposes lookup and group-selection helpers.

Because `MenuSource` is reused, templated controls can hold onto the same reference for the lifetime of the app and still observe item or selection changes driven by the builder.

#### MenuServices

`MenuServices` wraps two callbacks supplied by the builder: one to snapshot the lookup dictionary, and another to execute group-selection logic. Consumers get a simple API:

- `TryGetMenuItemById(string id, out MenuItemData? item)` – immediately resolves using the cached dictionary.
- `GetLookup()` – returns the latest snapshot, rebuilding only when the builder marked the hierarchy as dirty.
- `HandleGroupSelection(MenuItemData item)` – toggles checkable items or enforces radio-group exclusivity using the builder’s internal traversal helpers.

The services instance is entirely UI-agnostic, making it safe to reuse across templated controls, background logic, or automated tests.

#### Putting it together

```csharp
public sealed class MainViewModel : ObservableObject
{
    public IMenuSource MainMenu { get; }

    public MainViewModel()
    {
        MainMenu = new MenuBuilder()
            .AddSubmenu("&File", file => file
                .AddMenuItem("&New Project", command: NewProjectCommand, acceleratorText: "Ctrl+N")
                .AddMenuItem("&Open...", command: OpenCommand, acceleratorText: "Ctrl+O")
                .AddSeparator()
                .AddCheckableMenuItem("&Auto Save", isChecked: true))
            .AddSubmenu("&View", view => view
                .AddRadioMenuItem("&Light", "theme", isChecked: true)
                .AddRadioMenuItem("&Dark", "theme"))
            .Build();
    }

    public ObservableCollection<MenuItemData> Items => this.MainMenu.Items;
    public MenuServices Services => this.MainMenu.Services;
}

// XAML usage (once custom controls land)
// <controls:MenuBar MenuSource="{x:Bind ViewModel.MainMenu}" />
// <controls:MenuFlyout MenuSource="{x:Bind ViewModel.MainMenu}" />
```

#### Benefits of the data-first approach

- **Single source of truth**: `MenuBuilder` maintains hierarchy, identifiers, and lookup state in one place.
- **Predictable IDs**: deterministic naming simplifies telemetry, command routing, and deep-linking.
- **Shared services**: `MenuServices` is reused across controls, ensuring group logic stays consistent.
- **UI agnostic**: Templated controls, tests, and tooling all work with the same `IMenuSource` contract.
- **Extendable**: Additional presentations (like the forthcoming templated controls) plug into the same data stream without modifying builder logic.

---

## 3. Implementation Status & Roadmap

### ✅ **COMPLETED: Foundation**

**Shared Data Model** (✅ **FULLY IMPLEMENTED**):

- ✅ `MenuItemData` class with all required properties
- ✅ `MenuBuilder` with fluent API and lookup functionality
- ✅ Icon vs Selection State UX pattern
- ✅ Observable properties and data binding support

### 🔄 **NEEDED: Custom Controls**

**Custom MenuBar** (❌ **NEEDS IMPLEMENTATION**):

- ❌ Custom control with your column layout design
- ❌ Binds to MenuBuilder property
- ❌ Icon|Text|Accelerator|State alignment
- ❌ NO WinUI MenuBar usage - completely custom rendering

**Custom MenuFlyout** (❌ **NEEDS IMPLEMENTATION**):

- ❌ Custom popup control with same column layout
- ❌ Context menus and button flyouts
- ❌ Same presentation design as custom MenuBar

**Custom ExpandableMenuBar** (❌ **NEEDS IMPLEMENTATION**):

- ❌ Custom control with space-swapping hamburger ↔ menubar
- ❌ Binds to MenuBuilder property
- ❌ Same column layout as MenuBar when expanded
- ❌ IsExpanded property for programmatic control

### Summary: Custom Menu System Status

**What we have**:

- ✅ Complete data model (MenuItemData, MenuBuilder)
- ✅ Design specifications for custom controls
- ✅ Column layout requirements defined

**What we need**:

- ❌ Custom MenuBar control implementation
- ❌ Custom MenuFlyout control implementation
- ❌ Custom ExpandableMenuBar control implementation
- ❌ All using your custom presentation design, NO WinUI controls

---

## Next Steps: Custom Control Implementation

To finish the custom menu system, focus on the three container controls that compose the already-implemented `MenuItem` foundation:

1. **`<controls:MenuBar>`** - Horizontal menu bar container
2. **`<controls:MenuFlyout>`** - Popup context menu container
3. **`<controls:ExpandableMenuBar>`** - Space-swapping hamburger ↔ menubar container

Each container should:

- Bind to `MenuBuilder` property for data
- Use `CustomMenuItem` internally for item rendering
- Support hover navigation and selection states
- Render completely custom (NO WinUI MenuBar/MenuFlyout usage)

### MenuBar & MenuFlyout Implementation Plan

The following plan translates the high-level goals above into an incremental, best-practice implementation roadmap that keeps WinUI 3 guidance, Fluent v2 theming, and our existing data-first architecture front and center.

#### 1. Shared infrastructure

- **Introduce a `MenuInteractionController` helper** to coordinate hover, pointer corridor, keyboard navigation, and dismissal logic for both containers. It owns active column stacks, focus restoration, and works directly with `MenuServices` for group toggling.
- **Author a reusable `MenuColumnPresenter` control** that wraps an `ItemsRepeater` for vertical columns, uses `MenuItem` as the item template, and exposes events for hover/press. This ensures identical visuals between menubar submenus and flyouts.
- **Create a lightweight `MenuPopupHost` service** that manages popup surfaces (using `Popup` for menubar cascades and the flyout presenter for context menus), handles window bounds, RTL flipping, and density/scroll clamping.
- **Wire telemetry and accessibility** in the shared layer (UIA patterns, `AutomationProperties`, narrator announcements) so both surfaces inherit the same behavior.

#### 2. `MenuBar` control (public surface `DroidNet.Controls.MenuBar`)

- **Class skeleton**: derive from `Control`, set `DefaultStyleKey`, and expose dependency properties `MenuSource` (`IMenuSource`), `OpenRootIndex` (int, two-way), `IsSubmenuOpen` (bool), plus a read-only `ActiveNavigationMode` (Pointer/Keyboard) via `DependencyPropertyKey`.
- **Template parts** (enforced via `[TemplatePart]`):
  - `PART_RootHost` (`Grid`) containing layout chrome and keyboard focus scopes.
  - `PART_RootItemsRepeater` (`ItemsRepeater`) with a `StackLayout` horizontal orientation to render root items via `MenuItem` (using `MenuItemData` with `IsTopLevel=true`).
  - `PART_SubmenuOverlay` (`Grid` or `Canvas`) hosting cascading popup columns (leveraging `MenuColumnPresenter`).
  - `PART_FocusTracker` (`Control`) or hidden `ContentControl` to keep logical focus when pointer navigation dominates.
  - Optional `PART_AccessKeyOverlay` for Alt mnemonic display (align with WinUI AccessKeyManager).
- **Template styling**: define a dedicated `MenuBar/MenuBar.xaml` resource dictionary merged from `Themes/Generic.xaml`, referencing Fluent v2 tokens like `ControlFillColorDefaultBrush`, `TextFillColorPrimaryBrush`, `StrokeColorNeutralBrush` for separators, and high contrast-safe colors. Root background should respect transparent titlebar scenarios (use `ControlFillColorTransparentBrush`).
- **Interaction plumbing**:
  - On `MenuSource` change, bind `ItemsRepeater.ItemsSource` to `MenuSource.Items` and subscribe to `CollectionChanged` for dynamic rebuilds.
  - Hook `MenuItem` pointer and keyboard events to the `MenuInteractionController`, ensuring hover-to-open (`OpenRootIndex`) is zero-delay and keyboard navigation replicates Windows menu semantics.
  - Integrate `MenuServices.HandleGroupSelection` on invocation and bubble a `MenuItemInvoked` routed event mirroring the existing `MenuItem` event args.
  - Maintain a `DispatcherQueueTimer` for pointer corridor grace periods when moving between columns (configurable, default 150ms).
- **Visual states**: add `RootClosed/RootOpen`, `SubmenuIdle/SubmenuActive`, `PointerNavigation/KeyboardNavigation`, `AccessKeysVisible/AccessKeysHidden` and tie them to template parts for styling cues.
- **Focus & accessibility**: participate in `AccessKeyManager`, implement `OnProcessKeyboardAccelerators`, expose `MenuFlyoutItemAutomationPeer`-like automation peers for items, and ensure `IsTabStop=false` for container while root items are individually tabbable when menu is active.
- **Telemetry hooks**: optionally raise diagnostic events when cascades flip direction or when pointer corridor evicts a submenu to aid debugging.

#### 3. `MenuFlyout` control (public surface `DroidNet.Controls.MenuFlyout`)

- **Class skeleton**: derive from `FlyoutBase`, declare dependency properties `MenuSource` (`IMenuSource`) and `MaxColumnHeight` (`double`, default `Double.PositiveInfinity`). Provide `.ShowAt(FrameworkElement target)` overloads that delegate to base after priming data.
- **Presenter pipeline**:
  - Implement a sealed `MenuFlyoutPresenter` (derives from `FlyoutPresenter`) with template exposing parts `PART_PresenterRoot`, `PART_ItemsRepeater`, `PART_SubmenuOverlay`, and `PART_ScrollViewer`. Presenters leverage the same `MenuColumnPresenter` as menubar cascades.
  - Override `FlyoutBase.CreatePresenter` to return `MenuFlyoutPresenter` and feed it with `MenuInteractionController` instance scoped to each flyout opening.
- **Opening/closing life-cycle**:
  - Override `OnOpening/OnOpened/OnClosing/OnClosed` to initialize controller state, attach to `MenuSource.Services`, reset selection, and release references to avoid leaks.
  - Ensure keyboard focus is moved to first enabled item, with `Esc` dismissal and `Enter/Space` invocation consistent with menubar.
- **Theming**: add `MenuFlyout/MenuFlyout.xaml` resources with Fluent v2 brushes (`FlyoutBackgroundFillColorTertiaryBrush`, `ShadowElevationFlyout` etc.), use rounded corners via `CornerRadius` tokens, and ensure theme transitions via `ThemeResource`.
- **Sizing/scroll**: enforce `MaxColumnHeight` by wrapping the column presenter in a `ScrollViewer` with `VerticalScrollBarVisibility=Auto`, applying `ScrollViewers.ScrollBar*` theme resources for consistency. Use `SharedHelpers.SafeRect` to confine to window bounds.
- **Context integration**: support `XamlRoot` boundaries, `Target.AppWindow` overlay, and propagate `MenuItemInvoked` events to consumer via `ItemInvoked` event.

#### 4. Styling & resource delivery

- Add both resource dictionaries to `Themes/Generic.xaml` merge order after `MenuItem` to ensure base resources are available.
- Store layout constants (margins, corner radius, spacing) in dedicated resources (e.g., `MenuBarRootPadding`, `MenuColumnCornerRadius`) referencing Fluent v2 tokens or derived values.
- Provide high-contrast overrides using `x:Key="HighContrast"` resource dictionaries or `AppTheme` specific dictionaries if necessary.
- Create icon glyph resources (e.g., chevron, checkmark) once in shared dictionary to guarantee consistency with `MenuItem` glyphs.

#### 5. State management & command routing

- Centralize selection toggling, radio enforcement, and command execution in the shared controller, delegating to `MenuServices` to keep business logic data-first.
- For pointer-driven cascades, compute anchor rectangles via `UIElement.TransformToVisual(null)` to position submenu popups precisely relative to `XamlRoot`. Provide fallback for off-thread input by deferring to `Loaded` events.
- Support `MenuServices.TryGetMenuItemById` for telemetry and potential automation tests.

#### 6. Validation & test coverage

- **Unit tests**: expand `Controls.Menus.Tests` to cover controller state transitions (root switching, corridor timers, radio toggles) using headless test doubles of `MenuItemData`.
- **UI tests**: add WinUI UITest cases in `Controls.Menus.UI.Tests` verifying pointer hover switching, keyboard navigation (Alt key entry, arrow traversal), Flyout show/close, and theme brush application snapshots (light/dark/high contrast).
- **Accessibility**: run `AccessibilityInsights` automation in pipeline to confirm UIA roles/ patterns, and add tests that ensure `AutomationPeer` exposes `ControlType` of `MenuBar`/`Menu` appropriately.
- **Performance**: profile `ItemsRepeater` virtualization with 100+ items using `TraceLogging` to ensure opening cost stays under target (<5 ms per column on reference hardware).

#### 7. Delivery sequencing

- Implement shared infrastructure first, followed by `MenuBar`, then reuse for `MenuFlyout` to minimize duplication.
- Gate feature behind preview flag or internal namespace until stabilized; update samples/demo apps to showcase both controls.
- Document public API surface in `README.md` and provide minimal sample XAML demonstrating binding via `MenuBuilder`.
- Implement shared infrastructure first, followed by `MenuBar`, then reuse for `MenuFlyout` to minimize duplication.
- Gate feature behind preview flag or internal namespace until stabilized; update samples/demo apps to showcase both controls.
- Document public API surface in `README.md` and provide minimal sample XAML demonstrating binding via `MenuBuilder`.

---

## Current Implementation Analysis

The foundation is solid - MenuBuilder and MenuItemData provide a complete data model. What you need now are the FOUR custom controls that render this data with your specified presentation design.

**Current Status**:

- ✅ **MenuBuilder**: Complete fluent API and data model
- ✅ **MenuItemData**: All properties for icons, text, accelerators, selection states
- ✅ **Column Layout Design**: Icon|Text|Accelerator|State specifications defined
- ✅ **CustomMenuItem Control**: Implemented in `DroidNet.Controls.MenuItem` with tests
- ❌ **Container Controls**: MenuBar, MenuFlyout, ExpandableMenuBar that compose `MenuItem`

**Critical Path**: Implement the custom containers using the existing `MenuItem` control and shared data model.

---

## 4. Design Decisions & Rationale

### Q1: Parent Item Click Behavior

**Decision**: Parent items are **containers only** - they do not execute commands
**Rationale**: Prevents accidental command execution, makes click behavior predictable

### Q2: Hover Delay

**Decision**: **Zero delay** for root switching, minimal delay for submenus
**Rationale**: Root switching must be instant for speed, slight submenu delay prevents accidental expansion

### Q3: Animations

**Decision**: **Minimal animations** - only subtle fades for polish
**Rationale**: Speed is more important than visual effects

### Q4: Modifier Key Behavior

**Decision**: Menu **always closes** after command execution, regardless of modifiers
**Rationale**: Keeps behavior simple and predictable

### Q5: Integration Strategy

**Decision**: Extend existing `MenuBuilder` rather than replace
**Rationale**: Minimizes disruption to existing code while adding new capabilities

---

## 5. Success Metrics

**Performance Goals**:

- Menu appears immediately after expand button click
- Hover switching responds instantly
- Command execution begins without delay
- Menu dismissal is immediate

**User Experience Goals**:

- Expert users can execute known commands quickly
- New users can discover available commands through exploration
- Zero accidental command executions
- Works flawlessly with keyboard and screen readers

**Technical Requirements**:

- Supports unlimited nesting depth
- Handles 100+ items per menu without performance degradation
- Adapts to different screen sizes and orientations
- Integrates seamlessly with existing WinUI applications
