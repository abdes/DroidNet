# Ultra-Fast Custom Menu System

## Table of Contents

- [1. User Experience Model](#1-user-experience-model)
  - [1.1 System Entry - The Lightning Start](#11-system-entry---the-lightning-start)
  - [1.2 Lightning-Fast Hover Navigation - The Core Experience](#12-lightning-fast-hover-navigation)
  - [1.3 Command Execution - The Final Click](#13-command-execution---the-final-click)
  - [1.4 Smart Dismissal - Getting Out of the Way](#14-smart-dismissal---getting-out-of-the-way)
  - [1.5 Power User Keyboard Navigation](#15-power-user-keyboard-navigation)
  - [1.6 User Journey Examples](#16-user-journey-examples)
  - [1.7 Visual Design Requirements](#17-visual-design-requirements)
  - [1.8 Accessibility Support](#18-accessibility-support)
- [2. Technical Architecture](#2-technical-architecture)
  - [2.1 System Overview](#21-system-overview)
  - [2.2 Core Components](#22-core-components)
  - [2.3 Data Model - Optimized for Speed](#23-data-model---optimized-for-speed)
- [3. Implementation Roadmap](#3-implementation-roadmap)
- [4. Design Decisions & Rationale](#4-design-decisions--rationale)
- [5. Success Metrics](#5-success-metrics)

## Design Specification for Zero-Friction Navigation

### Design Philosophy

This menu system is designed for **power users who value speed over discovery**.
Every interaction is optimized for the user who knows exactly what they want and
gets there in the fewest possible actions.

**Core Principles:**

1. **One-Click Rule**: Only the expand button and final command require clicks
2. **Instant Response**: All navigation is hover-driven with zero perceived delay
3. **Predictable Behavior**: Consistent patterns that become muscle memory
4. **No Wasted Motion**: Every pointer movement serves a purpose

---

## 1. User Experience Model

### 1.1 System Entry - The Lightning Start

**User Goal**: Get into the menu system instantly and see relevant options immediately.

**Interaction Flow**:

```text
[User clicks ☰] → [Button vanishes] → [Menu appears with File open] → [Ready to navigate]
                    instantly        instantly                     immediate
```

**What Happens:**

- Single click on expand button (☰) - this is the ONLY required click for navigation
- Expand button disappears instantly (no animation delay)
- Menu overlay appears with:
  - Horizontal root menu bar (File, Edit, View, Tools, Help...)
  - First root menu (File) automatically expanded showing its items
  - Visual focus on "File" root button
  - Submenu positioned optimally (typically below root bar)

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

**User Goal**: Exit the menu system quickly when done, or when focus shifts.

**Automatic Dismissal** (No user action required):

```text
✓ Command executed        → Menu closes instantly
✓ Clicked outside menu    → Menu closes immediately
✓ Window lost focus       → Menu closes gracefully
```

**Manual Dismissal** (User-initiated exit):

```text
✓ Escape key             → Menu closes, focus returns to expand button
✓ Alt key (double-tap)   → Menu closes (optional feature)
```

**What Stays Open**:

- **Hover navigation**: Moving between menu items never closes the system
- **Brief mouse exit**: Accidentally moving outside menu area has small grace period
- **Keyboard focus**: Using arrow keys, Tab, etc. keeps menu open

**Critical Behavior**:

- **Instant response**: All dismissal triggers work immediately
- **Predictable**: User always knows how to exit
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

## 2. Technical Architecture

### Built for Speed and Reliability

## 2.1 System Overview

The menu system is designed as a **single, coordinated control** rather than
multiple independent flyouts. This ensures instant hover switching and
predictable behavior.

**Core Architecture**:

```text
┌─────────────────────────────────────────────────────────────────┐
│ MenuSystemHost (UserControl)                                   │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ Popup (LightDismiss=false)                                  │ │
│ │ ┌─────────────────────────────────────────────────────────┐ │ │
│ │ │ MenuHost (Canvas/Grid)                                  │ │ │
│ │ │ ┌─────────────┬─────────────┬─────────────┬───────────┐ │ │ │
│ │ │ │ RootMenuBar │ColumnPanel │ColumnPanel │    ...    │ │ │ │
│ │ │ │ File|Edit   │   Items     │   SubItems  │           │ │ │ │
│ │ │ └─────────────┴─────────────┴─────────────┴───────────┘ │ │ │
│ │ └─────────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

## 2.2 Core Components

### MenuSystemHost - WinUI Control

**Responsibility**: Renders MenuItem data model and handles user interaction

```csharp
[TemplatePart(Name = "PART_Popup", Type = typeof(Popup))]
[TemplatePart(Name = "PART_MenuHost", Type = typeof(Panel))]
[TemplatePart(Name = "PART_RootMenuBar", Type = typeof(Panel))]
public sealed class MenuSystemHost : Control
{
    // DependencyProperties for data binding
    public static readonly DependencyProperty ItemsProperty =
        DependencyProperty.Register(nameof(Items), typeof(ObservableCollection<MenuItem>),
            typeof(MenuSystemHost), new PropertyMetadata(null, OnItemsChanged));

    public static readonly DependencyProperty IsOpenProperty =
        DependencyProperty.Register(nameof(IsOpen), typeof(bool),
            typeof(MenuSystemHost), new PropertyMetadata(false, OnIsOpenChanged));

    // Routed Events following WinUI patterns
    public static readonly RoutedEvent ItemInvokedEvent =
        EventManager.RegisterRoutedEvent(nameof(ItemInvoked), RoutingStrategy.Bubble,
            typeof(TypedEventHandler<MenuSystemHost, MenuItemInvokedEventArgs>), typeof(MenuSystemHost));

    // CLR Properties
    public ObservableCollection<MenuItem> Items
    {
        get => (ObservableCollection<MenuItem>)GetValue(ItemsProperty);
        set => SetValue(ItemsProperty, value);
    }

    public bool IsOpen
    {
        get => (bool)GetValue(IsOpenProperty);
        set => SetValue(IsOpenProperty, value);
    }

    // Events
    public event TypedEventHandler<MenuSystemHost, MenuItemInvokedEventArgs> ItemInvoked
    {
        add => AddHandler(ItemInvokedEvent, value);
        remove => RemoveHandler(ItemInvokedEvent, value);
    }

    // Control lifecycle
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();
        // Initialize template parts and wire up event handlers
    }

    // Public API methods
    public void Open() => IsOpen = true;
    public void Close() => IsOpen = false;
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
    public void OpenSubmenu(string parentItemId, ObservableCollection<MenuItem> items);
    public void ExecuteCommand(MenuItem item);

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

### MenuItem - Data Model

**The foundation of the entire system** - a data model class that represents menu structure and commands:

```csharp
public sealed class MenuItem : ObservableObject
{
    // Core identity and display
    [ObservableProperty] private string text = string.Empty;
    [ObservableProperty] private IconSource? icon;
    [ObservableProperty] private string? inputGestureText;    // "Ctrl+S" display text
    [ObservableProperty] private char? mnemonic;              // 'F' for &File (Alt+F)

    // Command behavior
    [ObservableProperty] private ICommand? command;
    [ObservableProperty] private object? commandParameter;

    // State for checkable items
    [ObservableProperty] private bool isSelected;
    [ObservableProperty] private string? radioGroupId;        // For radio button groups

    // Hierarchy - ObservableCollection for proper change notifications
    public ObservableCollection<MenuItem> Items { get; } = new();

    // Computed properties for performance and convenience
    public string Id => Text.Replace('.', '_').ToUpperInvariant();
    public bool HasChildren => Items.Count > 0;
    public bool IsLeafItem => !HasChildren;
    public bool IsExecutable => Command != null;
    public bool IsCheckable => IsSelected || !string.IsNullOrEmpty(RadioGroupId);

    // Constructor for fluent initialization
    public MenuItem(string text = "")
    {
        Text = text;
    }
}
```

### MenuColumnState

**Tracks what's currently visible** - enables instant state restoration:

```csharp
internal sealed class MenuColumnState
{
    public string? ParentItemId { get; }                    // null for root
    public ObservableCollection<MenuItem> Items { get; }   // Observable for binding
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

- **Shared item references**: Same `MenuItem` used across multiple contexts
- **Lazy column creation**: Visual containers created only when needed
- **Event pooling**: Reuse event args objects to minimize allocations

### Unified MenuBuilder Design

The `MenuBuilder` becomes the **single source of truth** for all menu presentations. It builds the same menu data structure but can render it in different ways:

```csharp
public class MenuBuilder  // Enhanced existing class
{
    private readonly ObservableCollection<MenuItem> items = new();
    private Dictionary<string, MenuItem> itemsLookup = new();

    // Fluent building API
    public MenuBuilder AddMenuItem(string text, ICommand? command = null, IconSource? icon = null)
    {
        var item = new MenuItem(text) { Command = command, Icon = icon };
        items.Add(item);
        return this;
    }

    public MenuBuilder AddSubmenu(string text, Action<MenuBuilder> configureSubmenu, IconSource? icon = null)
    {
        var item = new MenuItem(text) { Icon = icon };
        var subBuilder = new MenuBuilder();
        configureSubmenu(subBuilder);

        foreach (var subItem in subBuilder.items)
            item.Items.Add(subItem);

        items.Add(item);
        return this;
    }

    public bool TryGetMenuItemById(string id, out MenuItem menuItem)
        => itemsLookup.TryGetValue(id, out menuItem);

    // Build methods for different presentations
    public ObservableCollection<MenuItem> BuildMenuItems() => items;  // For MenuSystemHost
    public MenuFlyout BuildMenuFlyout() { ... }                       // Context menus
    public MenuBar BuildMenuBar() { ... }                             // Traditional menu bars
}
```

**Design Philosophy:**

- **One data model**: All presentations use the same `MenuItem` hierarchy
- **One builder**: Single fluent API to construct menus
- **Multiple presentations**: Choose the UI style that fits your needs
- **Consistent behavior**: Same commands, same IDs, same logic everywhere

**Full Application Example:**

```csharp
// ViewModel builds complete application menu using fluent API
public class MainViewModel : ObservableObject
{
    public ObservableCollection<MenuItem> MenuItems { get; private set; }

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

// XAML Usage - Simple data binding to ObservableCollection<MenuItem>
// <local:MenuSystemHost Items="{x:Bind ViewModel.MenuItems}"
//                       ItemInvoked="OnMenuItemInvoked" />

// The same MenuBuilder can also create other presentations:
var menuBar = builder.BuildMenuBar();      // Traditional WinUI MenuBar
var contextMenu = builder.BuildMenuFlyout(); // Right-click context menu
```

**Three Presentations, One Menu System:**

1. **MenuSystemHost** (Ultra-fast hover menus):
   - Optimized for power users who know what they want
   - Instant hover navigation, minimal clicks
   - Compact overlay that appears on demand

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

## 3. Implementation Roadmap

### Phase 1: Foundation (Week 1)

**Goal**: Basic menu appears and responds to clicks

**Deliverables**:

- `MenuSystemHost` control with basic popup
- Enhanced `MenuItem` class with new properties (Icon, Mnemonic, etc.)
- `MenuController` skeleton
- Extended `MenuBuilder.BuildMenuSystem()` method
- Single-level menu display (root + one submenu)
- Click-to-execute leaf items
- Basic dismissal (Escape, outside click)

**Success Criteria**: Can open menu, see File submenu, click items, menu closes

### Phase 2: Hover Navigation (Week 2)

**Goal**: Instant hover switching between roots

**Deliverables**:

- Root menu hover detection and switching
- `MenuColumn` component with item rendering
- Visual states (hover, focus, disabled)
- Performance optimization for instant hover response

**Success Criteria**: Hover File→Edit→View switches instantly without flicker

### Phase 3: Cascading Submenus (Week 3)

**Goal**: Multi-level menu navigation

**Deliverables**:

- Recursive submenu opening on hover
- Dynamic column positioning and layout
- Smart overflow handling (left/right flip)
- Column cleanup when moving to siblings

**Success Criteria**: Navigate File→Recent→Project submenu with smooth hover

### Phase 4: Keyboard & Accessibility (Week 4)

**Goal**: Full keyboard navigation and screen reader support

**Deliverables**:

- Arrow key navigation (Up/Down/Left/Right)
- Mnemonic support (Alt+letter)
- Type-ahead search within menus
- AutomationPeer implementations for accessibility
- Focus management and visual indicators

**Success Criteria**: Complete menu navigation using only keyboard

### Phase 5: Polish & Performance (Week 5)

**Goal**: Production-ready experience

**Deliverables**:

- Visual polish (animations, theming)
- Performance profiling and optimization
- Edge case handling (RTL, high DPI, long menus)
- Comprehensive testing suite
- Documentation and examples

**Success Criteria**: Smooth, professional experience ready for users

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
