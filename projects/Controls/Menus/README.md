# DroidNet Fast Menu Controls

The menus package exposes a data-first API for building application menus that render consistently across the new `MenuBar` and `MenuFlyout` controls. Create your hierarchy once with `MenuBuilder`, then plug the shared `IMenuSource` into whichever surface you need.

```csharp
// ViewModel.cs
public partial class ShellViewModel : ObservableObject
{
    public ShellViewModel()
    {
        this.AppMenu = new MenuBuilder()
            .AddSubmenu("File", file => file
                .AddMenuItem("New", command: this.NewCommand)
                .AddMenuItem("Open…", command: this.OpenCommand)
                .AddSeparator()
                .AddMenuItem("Exit", command: this.ExitCommand))
            .AddSubmenu("View", view => view
                .AddMenuItem("Zoom In", command: this.ZoomInCommand)
                .AddMenuItem("Zoom Out", command: this.ZoomOutCommand)
                .AddSeparator()
                .AddMenuItem(new MenuItemData
                {
                    Text = "Theme",
                    SubItems = new[]
                    {
                        new MenuItemData { Text = "Light", RadioGroupId = "Theme", Command = this.SetLightThemeCommand },
                        new MenuItemData { Text = "Dark", RadioGroupId = "Theme", Command = this.SetDarkThemeCommand },
                    },
                }))
            .Build();
    }

    public IMenuSource AppMenu { get; }
}
```

```xml
<!-- ShellView.xaml -->
<Grid
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:controls="using:DroidNet.Controls">

    <Grid.RowDefinitions>
        <RowDefinition Height="Auto" />
        <RowDefinition Height="*" />
    </Grid.RowDefinitions>

    <controls:MenuBar
        Grid.Row="0"
        MenuSource="{x:Bind ViewModel.AppMenu}"
        ItemInvoked="OnMenuItemInvoked" />

    <Border Grid.Row="1" Margin="24" Background="{ThemeResource LayerFillColorDefaultBrush}">
        <TextBlock
            Margin="24"
            Style="{ThemeResource BodyTextBlockStyle}"
            Text="Content surface" />
    </Border>

    <Border
        x:Name="ContextTarget"
        Grid.Row="1"
        HorizontalAlignment="Left"
        VerticalAlignment="Top"
        Width="200"
        Height="120"
        Margin="32"
        Background="{ThemeResource LayerFillColorDefaultBrush}">
        <Border.ContextFlyout>
            <controls:MenuFlyout MenuSource="{x:Bind ViewModel.AppMenu}" />
        </Border.ContextFlyout>
    </Border>
</Grid>
```

Both containers reuse the shared `MenuInteractionController` so hover navigation, keyboard traversal, and radio-group coordination behave the same everywhere. Attach to the `ItemInvoked` events to react to command execution or close menus programmatically when needed.
