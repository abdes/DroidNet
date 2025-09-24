# OutputConsole Control

A high-performance, virtualized log viewer control for WinUI 3 applications. The
OutputConsole provides real-time log display with advanced filtering, search,
and customization capabilities, designed for developer tools, debugging
interfaces, and log monitoring applications.

## Features

- ✨ **Real-time Log Display**: Virtualized ListView for smooth performance with large log volumes
- 🔍 **Advanced Filtering**: Filter by log level, text content, source, channel, and exceptions
- 🎯 **Smart Highlighting**: Search term highlighting with culture-aware text matching
- ⏯️ **Pause/Resume**: Temporarily pause log updates while maintaining buffer integrity
- 📍 **Follow Tail**: Auto-scroll to latest entries with smart suspension when user scrolls away
- 🎨 **Theme Aware**: Adapts to light/dark themes with cached brush optimization
- ⌨️ **Keyboard Navigation**: F3/Shift+F3 for search navigation, Ctrl+F for focus search
- 📝 **Configurable Display**: Toggle timestamps, word wrapping, and level prefixes
- 🔗 **Serilog Integration**: Built-in sink for seamless Serilog configuration

## Installation

Add the OutputConsole control to your WinUI 3 project:

```xml
<PackageReference Include="DroidNet.Controls.OutputConsole" Version="x.x.x" />
```

## Quick Start

### 1. Basic XAML Usage

```xml
<controls:OutputConsoleView
    x:Name="LogConsole"
    ItemsSource="{x:Bind LogBuffer}"
    FollowTail="True"
    ShowTimestamps="True"
    WordWrap="False" />
```

### 2. Code-Behind Setup

```csharp
using DroidNet.Controls.OutputConsole;
using DroidNet.Controls.OutputConsole.Model;

public sealed partial class MainWindow : Window
{
    private readonly OutputLogBuffer logBuffer = new(capacity: 10000);

    public OutputLogBuffer LogBuffer => logBuffer;

    public MainWindow()
    {
        this.InitializeComponent();

        // Subscribe to console events
        LogConsole.ClearRequested += (_, _) => logBuffer.Clear();
        LogConsole.FollowTailChanged += OnFollowTailChanged;
        LogConsole.PauseChanged += OnPauseChanged;
    }

    private void OnFollowTailChanged(object sender, ToggleEventArgs e)
    {
        // Handle follow tail state changes
    }

    private void OnPauseChanged(object sender, ToggleEventArgs e)
    {
        // Handle pause state changes
    }
}
```

### 3. Serilog Integration

```csharp
using Serilog;
using DroidNet.Controls.OutputConsole;
using DryIoc;

// Configure Serilog with OutputConsole sink
var container = new Container();

Log.Logger = new LoggerConfiguration()
    .WriteTo.OutputConsole(container, capacity: 10000)
    .WriteTo.Console()
    .CreateLogger();

// Retrieve the shared buffer for UI binding
var logBuffer = container.Resolve<OutputLogBuffer>();
```

### 4. Manual Log Entry Creation

```csharp
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Events;

// Add log entries manually
logBuffer.Add(new OutputLogEntry
{
    Timestamp = DateTimeOffset.Now,
    Level = LogEventLevel.Information,
    Message = "Application started successfully",
    Source = "MainWindow",
    Channel = "UI"
});
```

## API Reference

### OutputConsoleView Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `ItemsSource` | `IEnumerable` | `null` | Source collection of log entries |
| `TextFilter` | `string` | `""` | Substring filter for log messages |
| `LevelFilter` | `LevelMask` | `All` | Bitmask for visible log levels |
| `SelectedItem` | `OutputLogEntry?` | `null` | Currently selected log entry |
| `FollowTail` | `bool` | `true` | Auto-scroll to newest entries |
| `IsPaused` | `bool` | `false` | Suspend live log updates |
| `ShowTimestamps` | `bool` | `false` | Display entry timestamps |
| `WordWrap` | `bool` | `false` | Enable text wrapping |

### Events

| Event | Type | Description |
|-------|------|-------------|
| `ClearRequested` | `EventHandler` | Fired when user requests log clear |
| `FollowTailChanged` | `EventHandler<ToggleEventArgs>` | Follow tail state changed |
| `PauseChanged` | `EventHandler<ToggleEventArgs>` | Pause state changed |

### OutputLogEntry Model

```csharp
public sealed class OutputLogEntry
{
    public DateTimeOffset Timestamp { get; init; }
    public LogEventLevel Level { get; init; }
    public string Message { get; init; } = string.Empty;
    public string? Source { get; init; }
    public string? Channel { get; init; }
    public Exception? Exception { get; init; }
    public IReadOnlyDictionary<string, object?> Properties { get; init; }
}
```

### LevelMask Enumeration

```csharp
[Flags]
public enum LevelMask
{
    None = 0,
    Verbose = 1,
    Debug = 2,
    Information = 4,
    Warning = 8,
    Error = 16,
    Fatal = 32,
    All = Verbose | Debug | Information | Warning | Error | Fatal
}
```

## Advanced Usage

### Custom Filtering

```csharp
// Programmatic filtering
LogConsole.LevelFilter = LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;
LogConsole.TextFilter = "authentication";

// Multi-field search (searches Message, Source, Channel, Exception)
LogConsole.TextFilter = "database";
```

### Performance Optimization

```csharp
// Use ring buffer with appropriate capacity
var buffer = new OutputLogBuffer(capacity: 50000);

// Pause updates during bulk operations
LogConsole.IsPaused = true;
// ... bulk log generation ...
LogConsole.IsPaused = false;
```

### Keyboard Shortcuts

- **Ctrl+F**: Focus search box
- **F3**: Find next occurrence
- **Shift+F3**: Find previous occurrence
- **Clear Button**: Trigger `ClearRequested` event

### Theme Customization

The control automatically adapts to WinUI 3 themes and caches brushes for optimal performance:

- `TextFillColorTertiaryBrush`: Verbose/Debug text
- `SystemFillColorCautionBrush`: Warning level
- `SystemFillColorCriticalBrush`: Error/Fatal levels
- `AccentTextFillColorPrimaryBrush`: Search highlights

## Best Practices

1. **Buffer Sizing**: Choose capacity based on expected log volume and retention needs
2. **Level Filtering**: Start with Information+ levels for better performance in high-volume scenarios
3. **Pause During Bulk**: Use `IsPaused` when expecting large log bursts
4. **Memory Management**: The ring buffer automatically manages memory by discarding old entries
5. **Culture Sensitivity**: Text filtering uses `CurrentCultureIgnoreCase` for user-entered queries

## Dependencies

- **Target Framework**: .NET 6+ with WinUI 3
- **Serilog**: Optional integration via `Serilog.Events`
- **DryIoc**: Optional container integration for shared buffer instances
