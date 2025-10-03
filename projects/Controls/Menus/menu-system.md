# DroidNet Menu System Documentation

This document describes the three menu systems provided by DroidNet.Controls.Menus, all built using the unified `MenuBuilder` and `MenuItemData` architecture.

## Custom Menu System Overview

**Philosophy**: Single data model (MenuBuilder) with consistent custom presentation across all menu types. No WinUI menu controls - completely custom rendering with your design specifications.

### 1. Custom MenuItem (❌ Needs Custom Implementation)

**What it is**: Individual menu item control that renders your column layout
**Usage**: Used internally by all menu controls
**Presentation**: Icon|Text|Accelerator|State columns with proper alignment
**Status**: ❌ **Needs custom control implementation - FOUNDATION for all menus**

### 2. Custom MenuBar (❌ Needs Custom Implementation)

**What it is**: Custom menu bar control with your presentation design
**Usage**: `<controls:MenuBar MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />`
**Presentation**: Always-visible horizontal menu with custom MenuItem rendering
**Status**: ❌ **Needs custom control implementation**

### 3. Custom MenuFlyout (❌ Needs Custom Implementation)

**What it is**: Custom popup menu control with your presentation design
**Usage**: `<controls:MenuFlyout MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />`
**Presentation**: Popup overlay with custom MenuItem rendering
**Status**: ❌ **Needs custom control implementation**

### 4. Custom ExpandableMenuBar (❌ Needs Custom Implementation)

**What it is**: Custom collapsible menu bar with hamburger button
**Usage**: `<controls:ExpandableMenuBar MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />`
**Presentation**: Space-swapping hamburger ⟷ menu bar with custom MenuItem rendering
**Status**: ❌ **Needs custom control implementation**

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

### 1. Custom MenuBar

**Purpose**: Always-visible menu bar with your custom presentation
**XAML Usage**: `<controls:MenuBar MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />`
**Behavior**: Click root → dropdown with custom item rendering

**Implementation**:

```csharp
// ViewModel - Single source of truth
public MenuBuilder MenuBuilder { get; } = new MenuBuilder()
    .AddMenuItem(new MenuItemData {
        Text = "&File",
        SubItems = [
            new MenuItemData { Text = "&New Project", Icon = new SymbolIconSource { Symbol = Symbol.Add }, AcceleratorText = "Ctrl+N", Command = NewCommand },
            new MenuItemData { Text = "&Save", Icon = new SymbolIconSource { Symbol = Symbol.Save }, AcceleratorText = "Ctrl+S", Command = SaveCommand },
            new MenuItemData { IsSeparator = true },
            new MenuItemData { Text = "&Auto Save", Icon = new FontIconSource { Glyph = "\uE74E" }, IsCheckable = true, IsSelected = true }
        ]
    });

// XAML - Custom control binding to MenuBuilder
// <controls:MenuBar MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />
```

### 2. Custom MenuFlyout

**Purpose**: Custom popup menus with your presentation design
**XAML Usage**: `<controls:MenuFlyout MenuBuilder="{x:Bind ViewModel.MenuBuilder}" />`
**Behavior**: Right-click or button press → popup with custom item rendering

**Implementation**:

```csharp
// Same MenuBuilder - different presentation
public MenuBuilder ContextMenuBuilder { get; } = new MenuBuilder()
    .AddMenuItem(new MenuItemData { Text = "Cut", Icon = new SymbolIconSource { Symbol = Symbol.Cut }, AcceleratorText = "Ctrl+X", Command = CutCommand })
    .AddMenuItem(new MenuItemData { Text = "Copy", Icon = new SymbolIconSource { Symbol = Symbol.Copy }, AcceleratorText = "Ctrl+C", Command = CopyCommand })
    .AddMenuItem(new MenuItemData { Text = "Paste", Icon = new ImageIconSource { UriSource = new Uri("ms-appx:///Assets/paste-icon.png") }, AcceleratorText = "Ctrl+V", Command = PasteCommand })
    .AddMenuItem(new MenuItemData { IsSeparator = true })
    .AddMenuItem(new MenuItemData { Text = "Properties", Icon = new FontIconSource { Glyph = "\uE713", FontFamily = new FontFamily("Segoe MDL2 Assets") }, Command = PropertiesCommand });

// XAML - Custom control with same column layout as MenuBar
// <TextBox>
//   <TextBox.ContextFlyout>
//     <controls:MenuFlyout MenuBuilder="{x:Bind ViewModel.ContextMenuBuilder}" />
//   </TextBox.ContextFlyout>
// </TextBox>
```

### 3. Custom ExpandableMenuBar

**Purpose**: Space-efficient hamburger that expands to full menu bar in title bar
**XAML Usage**: `<controls:ExpandableMenuBar MenuBuilder="{x:Bind ViewModel.MenuBuilder}" IsExpanded="{x:Bind IsMenuExpanded, Mode=TwoWay}" />`
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

**Implementation**:

```csharp
// Same MenuBuilder - space-swapping presentation
public MenuBuilder MainMenuBuilder { get; } = new MenuBuilder()
    .AddMenuItem(new MenuItemData { Text = "File", Items = FileMenuItems })
    .AddMenuItem(new MenuItemData { Text = "Edit", Items = EditMenuItems })
    .AddMenuItem(new MenuItemData { Text = "View", Items = ViewMenuItems })
    .AddMenuItem(new MenuItemData { Text = "Help", Items = HelpMenuItems });

public bool IsMenuExpanded { get; set; } = false;

// XAML - Custom control that transforms hamburger ↔ menubar in title bar
// <controls:ExpandableMenuBar
//     MenuBuilder="{x:Bind ViewModel.MainMenuBuilder}"
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
//   <TextBlock Grid.Column="3" Text="✓" Visibility="{Binding MenuItemData.IsSelected}" />
// </Grid>
```

**Why This Is Critical**: All three menu controls (MenuBar, MenuFlyout, ExpandableMenuBar) use CustomMenuItem internally to render individual menu items. This ensures consistent presentation across all menu types.

## Custom Menu System Comparison

| Feature | CustomMenuItem | Custom MenuBar | Custom MenuFlyout | Custom ExpandableMenuBar |
|---------|---------|---------|------------|-------------------|
| **Status** | 🔥 **CRITICAL - Foundation** | 🔄 **Need Custom Control** | 🔄 **Need Custom Control** | 🔄 **Need Custom Control** |
| **Purpose** | Individual item rendering | Menu container | Popup container | Space-swapping container |
| **Data Source** | Single MenuItemData | MenuBuilder | MenuBuilder | MenuBuilder |
| **Presentation** | Icon\|Text\|Accel\|State columns | Horizontal menu using CustomMenuItem | Popup using CustomMenuItem | Hamburger ↔ menubar using CustomMenuItem |
| **Use Case** | Foundation for all menus | Always-visible app menus | Context/popup menus | Title bar space-efficient menus |
| **Appearance** | Individual menu item | Top of window menu bar | Popup on demand | Hamburger button + space-swapping |
| **Activation** | Hover/click | Click menu root | Right-click or button | Click hamburger (☰) button |
| **Dismissal** | N/A - part of container | Click elsewhere | Select item or click outside | Select item, Escape, or click outside |
| **Implementation** | **CUSTOM CONTROL NEEDED** | **CUSTOM CONTROL NEEDED** | **CUSTOM CONTROL NEEDED** | **CUSTOM CONTROL NEEDED** |

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
    public bool IsSelected { get; set; }               // Current selection state
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
            item.IsSelected = false;  // Clear all checkmarks in group
        }

        selectedItem.IsSelected = true;  // Set checkmark on selected item only
    }
    else if (selectedItem.IsCheckable)
    {
        // Individual checkable item - just toggle
        selectedItem.IsSelected = !selectedItem.IsSelected;
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
- **Behavior**: Checkmark (✓) appears/disappears based on IsSelected
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
- Checkmarks (✓) appear in State column when `IsSelected = true`
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
    IsSelected = true,  // Shows checkmark
    Command = ToggleAutoSaveCommand
}

// Grouped item (mutually exclusive selection - uses checkmark, NOT radio button)
new MenuItemData
{
    Text = "Light Theme",
    RadioGroupId = "Theme",
    IsSelected = true,  // Shows checkmark ✓ (code-behind ensures only one per group)
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

### Unified MenuBuilder Design

The `MenuBuilder` becomes the **single source of truth** for all menu presentations. It builds the same menu data structure but can render it in different ways:

```csharp
public class MenuBuilder  // Enhanced existing class
{
    private readonly ObservableCollection<MenuItemData> items = new();
    private Dictionary<string, MenuItemData> itemsLookup = new();

    // Fluent building API
    public MenuBuilder AddMenuItem(string text, ICommand? command = null, IconSource? icon = null)
    {
        var item = new MenuItemData(text) { Command = command, Icon = icon };
        items.Add(item);
        return this;
    }

    public MenuBuilder AddSubmenu(string text, Action<MenuBuilder> configureSubmenu, IconSource? icon = null)
    {
        var item = new MenuItemData(text) { Icon = icon };
        var subBuilder = new MenuBuilder();
        configureSubmenu(subBuilder);

        foreach (var subItem in subBuilder.items)
            item.Items.Add(subItem);

        items.Add(item);
        return this;
    }

    public bool TryGetMenuItemById(string id, out MenuItemData menuItem)
        => itemsLookup.TryGetValue(id, out menuItem);

    // Build methods for custom controls
    public ObservableCollection<MenuItemData> BuildMenuItems() => items;     // For all custom controls
    // Note: Custom controls bind directly to MenuBuilder property - no Build methods needed
}
```

**Design Philosophy:**

- **One data model**: All presentations use the same `MenuItemData` hierarchy
- **One builder**: Single fluent API to construct menus
- **Multiple presentations**: Choose the UI style that fits your needs
- **Consistent behavior**: Same commands, same IDs, same logic everywhere

**Full Application Example:**

```csharp
// ViewModel builds complete application menu using fluent API
public class MainViewModel : ObservableObject
{
    public ObservableCollection<MenuItemData> MenuItems { get; private set; }

    public MainViewModel()
    {
        MenuItems = new MenuBuilder()
            .AddSubmenu("&File", file => file
                .AddMenuItem("&New Project", NewProjectCommand, new FontIcon { Glyph = "\uE8A5" })
                .AddMenuItem("&Open...", OpenCommand, new FontIcon { Glyph = "\uE8E5" })
                .AddSubmenu("&Recent Files", recent => recent
                    .AddMenuItem("Project1.proj", OpenRecentCommand)
                    .AddMenuItem("Document.txt", OpenRecentCommand))
                .AddMenuItem("-") // Separator
                .AddMenuItem("&Save", SaveCommand, new FontIcon { Glyph = "\uE74E" })
                .AddMenuItem("Save &As...", SaveAsCommand)
                .AddMenuItem("-")
                .AddMenuItem("E&xit", ExitCommand))
            .AddSubmenu("&Edit", edit => edit
                .AddMenuItem("&Undo", UndoCommand, new FontIcon { Glyph = "\uE7A7" })
                .AddMenuItem("&Redo", RedoCommand, new FontIcon { Glyph = "\uE7A6" })
                .AddMenuItem("-")
                .AddMenuItem("Cu&t", CutCommand, new FontIcon { Glyph = "\uE8C6" })
                .AddMenuItem("&Copy", CopyCommand, new FontIcon { Glyph = "\uE8C8" })
                .AddMenuItem("&Paste", PasteCommand, new FontIcon { Glyph = "\uE77F" }))
            .AddSubmenu("&View", view => view
                .AddMenuItem("&Zoom In", ZoomInCommand)
                .AddMenuItem("Zoom &Out", ZoomOutCommand)
                .AddMenuItem("&Reset Zoom", ResetZoomCommand)
                .AddMenuItem("-")
                .AddSubmenu("&Panels", panels => panels
                    .AddMenuItem("&Tool Panel", ToggleToolPanelCommand)
                    .AddMenuItem("&Properties Panel", TogglePropertiesPanelCommand)
                    .AddMenuItem("&Output Panel", ToggleOutputPanelCommand)))
            .BuildMenuItems();
    }

    // Commands (ICommand implementations)
    public ICommand NewProjectCommand { get; }
    public ICommand OpenCommand { get; }
    public ICommand SaveCommand { get; }
    // ... etc
}

// XAML Usage - Simple data binding to ObservableCollection<MenuItemData>
// <local:ExpandableMenuBar Items="{x:Bind ViewModel.MenuItems}"
//                          ItemInvoked="OnMenuItemInvoked" />

// The same MenuBuilder works with all custom controls:
// <controls:ExpandableMenuBar MenuBuilder="{x:Bind MenuBuilder}" />
// <controls:MenuBar MenuBuilder="{x:Bind MenuBuilder}" />
// <controls:MenuFlyout MenuBuilder="{x:Bind MenuBuilder}" />
```

**Three Presentations, One Menu System:**

1. **ExpandableMenuBar** (Ultra-fast space-saving menus):
   - Optimized for power users who know what they want
   - Instant hover navigation, minimal clicks
   - Collapsible design saves screen space
   - Hamburger button reveals full menu with auto-expansion

2. **MenuBar** (Traditional discovery):
   - Familiar top-level menu bar
   - Good for new users exploring features
   - Always visible for discoverability

3. **MenuFlyout** (Context-sensitive):
   - Right-click context menus
   - Filtered to show only relevant commands
   - Same menu items, different filtering logic

**Benefits of this unified approach:**

- **Single source of truth**: Menu structure defined once, used everywhere
- **Consistent commands**: Same ICommand instances across all presentations
- **Unified lookup**: `menuBuilder.TryGetMenuItemById()` works for all presentations
- **Zero duplication**: No need to maintain separate menu definitions
- **Developer choice**: Pick the presentation that fits each UI context
- **User choice**: Power users get speed, casual users get discoverability

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

To complete the custom menu system, you need to implement FOUR custom controls:

**FOUNDATION CONTROL** (Build this first):

1. **`<controls:CustomMenuItem>`** - Individual menu item with your column layout
   - Icon|Text|Accelerator|State 4-column grid layout
   - Handles hover states, selection indicators, separators
   - Used by all other menu controls for consistent rendering

**CONTAINER CONTROLS** (Use CustomMenuItem internally):

1. **`<controls:MenuBar>`** - Horizontal menu bar container
2. **`<controls:MenuFlyout>`** - Popup context menu container
3. **`<controls:ExpandableMenuBar>`** - Space-swapping hamburger ↔ menubar container

All container controls should:

- Bind to `MenuBuilder` property for data
- Use `CustomMenuItem` internally for item rendering
- Support hover navigation and selection states
- Render completely custom (NO WinUI MenuBar/MenuFlyout usage)

---

## Current Implementation Analysis

The foundation is solid - MenuBuilder and MenuItemData provide a complete data model. What you need now are the FOUR custom controls that render this data with your specified presentation design.

**Current Status**:

- ✅ **MenuBuilder**: Complete fluent API and data model
- ✅ **MenuItemData**: All properties for icons, text, accelerators, selection states
- ✅ **Column Layout Design**: Icon|Text|Accelerator|State specifications defined
- ❌ **CustomMenuItem Control**: FOUNDATION control for individual item rendering
- ❌ **Container Controls**: MenuBar, MenuFlyout, ExpandableMenuBar that use CustomMenuItem

**Critical Path**: Build CustomMenuItem first since all other menu controls depend on it for consistent item presentation.

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
