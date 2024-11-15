# Docking Framework for .NET/WinUI Applications

This project is a docking framework for .NET/WinUI applications that provides a flexible and feature-rich system for managing dockable panels.

This framework provides a comprehensive solution for applications requiring sophisticated window management and docking capabilities, similar to what you might find in IDEs or professional applications.

## Features

### Docking Management
- Managed through the `Docker` class
- Supports multiple docking positions and orientations
- Tree-based layout management system
- Flexible docking operations (dock, undock, float, minimize)

### Dockable Panels
- Two main types:
  - `CenterDock` - For main application content
  - `ToolDock` - For tool windows/panels
- Support for multiple dockable items within a single dock
- Tab-based interface for multiple items

### Layout Features
- Resizable panels using `ResizableVectorGrid`
- Grid-based flow layout system
- Support for both horizontal and vertical orientations
- Stretch-to-fill capability

### UI Components
- `DockPanel` - Main docking panel control
- `DockTray` - For minimized panels
- Custom styled buttons and controls

## Architecture

### MVVM Pattern
- View models for panels (`DockPanelViewModel`)
- Observable collections for dockable items
- Command-based interactions

### Core Interfaces
- `IDock` - Dockable panel interface
- `IDockable` - Dockable item interface
- `IDocker` - Main docking manager interface

### Layout Management
- Tree-based layout structure
- Binary tree nodes for layout management
- Support for complex docking arrangements

## Operations

### Panel Operations
- Docking/Undocking
- Floating windows
- Minimizing/Restoring
- Resizing
- Tab management

### Layout Control
- Dynamic layout updates
- Automatic layout optimization
- Layout persistence support

### View Management
- Active view tracking
- View state management
- Multiple view support within docks
