// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Controls.Menus;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.Editors.Scene;
using Oxygen.Editor.WorldEditor.Engine;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// ViewModel for the Viewport control, managing camera, shading, and menus.
/// </summary>
public partial class ViewportViewModel : ObservableObject, IDisposable
{
    private readonly ISettingsService<IAppearanceSettings> appearanceSettings;

    // Lazy-backed menu sources — build on first access.
    private IMenuSource? viewMenu;
    private IMenuSource? shadingMenu;
    private IMenuSource? layoutMenu;

    // Optional override for the effective theme (set by the view). When present this will be
    // preferred for ThemeDictionary selection over Application.RequestedTheme.
    private ElementTheme? effectiveThemeOverride;

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="ViewportViewModel"/> class.
    /// </summary>
    /// <param name="documentId">The owning document identifier.</param>
    /// <param name="engineService">The shared engine service.</param>
    /// <param name="appearanceSettings">
    ///     The <see cref="ISettingsService{IAppearanceSettings}" /> used to provide appearance and theme settings.
    ///     This service supplies the current theme and notifies the view model of changes.
    /// </param>
    /// <param name="loggerFactory">
    ///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
    ///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
    /// </param>
    public ViewportViewModel(Guid documentId, IEngineService engineService, ISettingsService<IAppearanceSettings> appearanceSettings, ILoggerFactory? loggerFactory = null)
    {
        this.DocumentId = documentId;
        this.EngineService = engineService;
        this.LoggerFactory = loggerFactory;
        this.appearanceSettings = appearanceSettings;

        // Seed effective theme from settings and subscribe for changes.
        Debug.Assert(this.appearanceSettings != null, "appearanceSettings must not be null");
        this.SetEffectiveTheme(this.appearanceSettings.Settings.AppThemeMode);
        this.appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;
    }

    // Overlay view toggles
    [ObservableProperty]
    public partial bool ShowFps { get; set; }

    [ObservableProperty]
    public partial bool ShowStats { get; set; }

    [ObservableProperty]
    public partial bool ShowToolbar { get; set; }

    // Individual stat toggles
    [ObservableProperty]
    public partial bool Stat1 { get; set; }

    [ObservableProperty]
    public partial bool Stat2 { get; set; }

    [ObservableProperty]
    public partial bool Stat3 { get; set; }

    /// <summary>
    /// Gets or sets the camera type.
    /// </summary>
    [ObservableProperty]
    public partial CameraType CameraType { get; set; } = CameraType.Perspective;

    /// <summary>
    /// Gets or sets the shading mode.
    /// </summary>
    [ObservableProperty]
    public partial ShadingMode ShadingMode { get; set; } = ShadingMode.Wireframe;

    /// <summary>
    /// Gets or sets a value indicating whether the viewport is maximized.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MaximizeGlyph))]
    public partial bool IsMaximized { get; set; }

    /// <summary>
    /// Gets the document identifier owning this viewport.
    /// </summary>
    public Guid DocumentId { get; }

    /// <summary>
    /// Gets the unique viewport identifier.
    /// </summary>
    public Guid ViewportId { get; } = Guid.NewGuid();

    /// <summary>
    /// Gets the engine service reference, enabling views to request surfaces.
    /// </summary>
    public IEngineService EngineService { get; }

    /// <summary>
    /// Gets the zero-based viewport index in the current layout.
    /// </summary>
    public int ViewportIndex { get; private set; }

    /// <summary>
    /// Gets a value indicating whether this viewport should be considered primary.
    /// </summary>
    public bool IsPrimaryViewport { get; private set; }

    /// <summary>
    /// Gets the menu source for the View menu. Built lazily on first access.
    /// </summary>
    public IMenuSource ViewMenu => this.viewMenu ??= this.BuildViewMenu();

    /// <summary>
    /// Gets the menu source for the Shading menu. Built lazily on first access.
    /// </summary>
    public IMenuSource ShadingMenu => this.shadingMenu ??= this.BuildShadingMenu();

    /// <summary>
    /// Gets or sets the command to toggle maximize state.
    /// </summary>
    public ICommand? ToggleMaximizeCommand { get; set; }

    /// <summary>
    /// Gets the menu source for the Layout menu. Built lazily on first access.
    /// </summary>
    public IMenuSource LayoutMenu => this.layoutMenu ??= this.BuildLayoutMenu();

    /// <summary>
    /// Gets or sets callback invoked when a layout is requested from the viewport's layout menu.
    /// </summary>
    public Action<SceneViewLayout>? OnLayoutRequested { get; set; }

    /// <summary>
    /// Gets the glyph for the maximize/restore button.
    /// </summary>
    public string MaximizeGlyph => this.IsMaximized ? "\uE923" : "\uE922";

    /// <summary>
    /// Gets the logger factory.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; }

    /// <summary>
    /// Dispose of transient subscriptions.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Builds the surface request describing this viewport.
    /// </summary>
    /// <param name="tag">Optional diagnostic tag.</param>
    /// <returns>The surface request payload.</returns>
    public ViewportSurfaceRequest CreateSurfaceRequest(string? tag = null)
        => new()
        {
            DocumentId = this.DocumentId,
            ViewportId = this.ViewportId,
            ViewportIndex = this.ViewportIndex,
            IsPrimary = this.IsPrimaryViewport,
            Tag = tag,
        };

    /// <summary>
    /// Updates the layout metadata for the viewport, setting its index and primary status.
    /// </summary>
    /// <param name="index">The zero-based index of the viewport in the current layout.</param>
    /// <param name="isPrimary">True if this viewport is the primary viewport; otherwise, false.</param>
    internal void UpdateLayoutMetadata(int index, bool isPrimary)
    {
        this.ViewportIndex = index;
        this.IsPrimaryViewport = isPrimary;
    }

    /// <summary>
    /// Protected dispose pattern implementation.
    /// </summary>
    /// <param name="disposing">True if called from Dispose; false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
                this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_PropertyChanged;
            }

            this.isDisposed = true;
        }
    }

    /// <summary>
    /// Set the effective theme used for selecting themed resources (Light/Dark).
    /// Call this from the view (code-behind) using the view's ActualTheme.
    /// </summary>
    /// <param name="theme">The theme reported by the view (ActualTheme).</param>
    private void SetEffectiveTheme(ElementTheme theme)
    {
        // If menus were already built, rebuild them now so the UI updates
        // immediately with icons from the new theme. If menus haven't been
        // built yet we keep them lazy and they'll be created with the correct
        // theme on first access.
        var hadViewMenu = this.viewMenu is not null;
        var hadShadingMenu = this.shadingMenu is not null;
        var hadLayoutMenu = this.layoutMenu is not null;

        this.effectiveThemeOverride = theme;
        Debug.WriteLine($"[ViewportViewModel] Effective theme override set to {theme}");

        try
        {
            if (hadViewMenu)
            {
                this.viewMenu = this.BuildViewMenu();
                this.OnPropertyChanged(nameof(this.ViewMenu));
            }

            if (hadShadingMenu)
            {
                this.shadingMenu = this.BuildShadingMenu();
                this.OnPropertyChanged(nameof(this.ShadingMenu));
            }

            if (hadLayoutMenu)
            {
                this.layoutMenu = this.BuildLayoutMenu();
                this.OnPropertyChanged(nameof(this.LayoutMenu));
            }

            if (hadViewMenu || hadShadingMenu || hadLayoutMenu)
            {
                Debug.WriteLine("[ViewportViewModel] Rebuilt menus after effective theme override set.");
            }
            else
            {
                Debug.WriteLine("[ViewportViewModel] Effective theme set; menus remain lazy until first access.");
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[ViewportViewModel] Error rebuilding menus: {ex}");
        }
    }

    private void AppearanceSettings_PropertyChanged(object? sender, PropertyChangedEventArgs? e)
    {
        if (string.Equals(e?.PropertyName, nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal))
        {
            var theme = this.appearanceSettings.Settings.AppThemeMode;
            this.SetEffectiveTheme(theme);
        }
    }

    private IconSource? ResolveIcon(string name)
    {
        // The settings service seeds the VM's effective theme; assert it's present.
        Debug.Assert(!string.IsNullOrWhiteSpace(name), "name must be provided");
        Debug.Assert(this.effectiveThemeOverride.HasValue, "effectiveThemeOverride must be set by the settings service");

        var app = Application.Current;
        if (app is null)
        {
            return null;
        }

        var preferred = this.effectiveThemeOverride.Value == ElementTheme.Dark ? "Dark" : "Light";
        var key = $"Icon.{name}";

        // Only look in the ThemeDictionary that matches the effective theme.
        foreach (var md in app.Resources.MergedDictionaries)
        {
            if (md?.ThemeDictionaries is not { } td)
            {
                continue;
            }

            if (td.TryGetValue(preferred, out var pdObj) && pdObj is ResourceDictionary pd && pd.TryGetValue(key, out var pdVal) && pdVal is IconSource pdIcon)
            {
                return pdIcon;
            }
        }

        // No fallback: if the icon isn't found in the matching ThemeDictionary return null.
        return null;
    }

    private IMenuSource BuildViewMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        _ = builder
            .AddRadioMenuItem("Perspective", "CameraType", this.CameraType == CameraType.Perspective, new RelayCommand(() => this.CameraType = CameraType.Perspective))
            .AddSeparator()
            .AddRadioMenuItem("Top", "CameraType", this.CameraType == CameraType.Top, new RelayCommand(() => this.CameraType = CameraType.Top), icon: null)
            .AddRadioMenuItem("Bottom", "CameraType", this.CameraType == CameraType.Bottom, new RelayCommand(() => this.CameraType = CameraType.Bottom), icon: null)
            .AddRadioMenuItem("Left", "CameraType", this.CameraType == CameraType.Left, new RelayCommand(() => this.CameraType = CameraType.Left), icon: null)
            .AddRadioMenuItem("Right", "CameraType", this.CameraType == CameraType.Right, new RelayCommand(() => this.CameraType = CameraType.Right), icon: null)
            .AddRadioMenuItem("Front", "CameraType", this.CameraType == CameraType.Front, new RelayCommand(() => this.CameraType = CameraType.Front), icon: null)
            .AddRadioMenuItem("Back", "CameraType", this.CameraType == CameraType.Back, new RelayCommand(() => this.CameraType = CameraType.Back), icon: null);

        return builder.Build();
    }

    private IMenuSource BuildShadingMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        _ = builder
            .AddRadioMenuItem("Wireframe", "ShadingMode", this.ShadingMode == ShadingMode.Wireframe, new RelayCommand(() => this.ShadingMode = ShadingMode.Wireframe))
            .AddRadioMenuItem("Shaded", "ShadingMode", this.ShadingMode == ShadingMode.Shaded, new RelayCommand(() => this.ShadingMode = ShadingMode.Shaded))
            .AddRadioMenuItem("Rendered", "ShadingMode", this.ShadingMode == ShadingMode.Rendered, new RelayCommand(() => this.ShadingMode = ShadingMode.Rendered));

        return builder.Build();
    }

    private IMenuSource BuildLayoutMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        // Top-level toggles
        _ = builder.AddMenuItem(new MenuItemData
        {
            Text = "Show FPS",
            IsCheckable = true,
            IsChecked = this.ShowFps,
            AcceleratorText = "Ctrl+Shift+H",
            Command = new RelayCommand<MenuItemData?>(this.ToggleShowFps),
        })
        .AddMenuItem(new MenuItemData
        {
            Text = "Show Stats",
            IsCheckable = true,
            IsChecked = this.ShowStats,
            AcceleratorText = "Shift+L",
            Command = new RelayCommand<MenuItemData?>(this.ToggleShowStats),
        })
        .AddSubmenu("Stats", submenu => submenu
            .AddMenuItem(new MenuItemData
            {
                Text = "Stat1",
                IsCheckable = true,
                IsChecked = this.Stat1,
                Command = new RelayCommand<MenuItemData?>(this.ToggleStat1),
            })
            .AddMenuItem(new MenuItemData
            {
                Text = "Stat2",
                IsCheckable = true,
                IsChecked = this.Stat2,
                Command = new RelayCommand<MenuItemData?>(this.ToggleStat2),
            })
            .AddMenuItem(new MenuItemData
            {
                Text = "Stat3",
                IsCheckable = true,
                IsChecked = this.Stat3,
                Command = new RelayCommand<MenuItemData?>(this.ToggleStat3),
            }))
        .AddMenuItem(new MenuItemData
        {
            Text = "Show Toolbar",
            IsCheckable = true,
            IsChecked = this.ShowToolbar,
            AcceleratorText = "Ctrl+Shift+T",
            Command = new RelayCommand<MenuItemData?>(this.ToggleShowToolbar),
        })
        .AddSeparator();

        // Layouts submenu with grouped panes and themed icons
        _ = builder.AddSubmenu("Layouts", layouts => layouts
            .AddMenuItem(
                "One Pane",
                new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.OnePane)),
                this.ResolveIcon("OnePane"))
            .AddMenuItem(
                "Four Quadrants",
                new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourQuad)),
                this.ResolveIcon("FourQuad"))
            .AddSubmenu("Two Panes", two => _ = two
                .AddMenuItem(
                    "Main Left",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainLeft)),
                    this.ResolveIcon("TwoMainLeft"))
                .AddMenuItem(
                    "Main Right",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainRight)),
                    this.ResolveIcon("TwoMainRight"))
                .AddMenuItem(
                    "Main Top",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainTop)),
                    this.ResolveIcon("TwoMainTop"))
                .AddMenuItem(
                    "Main Bottom",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainBottom)),
                    this.ResolveIcon("TwoMainBottom")))
            .AddSubmenu("Three Panes", three => three
                .AddMenuItem(
                    "Main Left",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainLeft)),
                    this.ResolveIcon("ThreeMainLeft"))
                .AddMenuItem(
                    "Main Right",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainRight)),
                    this.ResolveIcon("ThreeMainRight"))
                .AddMenuItem(
                    "Main Top",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainTop)),
                    this.ResolveIcon("ThreeMainTop"))
                .AddMenuItem(
                    "Main Bottom",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainBottom)),
                    this.ResolveIcon("ThreeMainBottom")))
            .AddSubmenu("Four Panes", four => four
                .AddMenuItem(
                    "Main Left",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainLeft)),
                    this.ResolveIcon("FourMainLeft"))
                .AddMenuItem(
                    "Main Right",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainRight)),
                    this.ResolveIcon("FourMainRight"))
                .AddMenuItem(
                    "Main Top",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainTop)),
                    this.ResolveIcon("FourMainTop"))
                .AddMenuItem(
                    "Main Bottom",
                    new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainBottom)),
                    this.ResolveIcon("FourMainBottom"))));

        return builder.Build();
    }

    private void ToggleShowFps(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.ShowFps = menuItem.IsChecked;
    }

    private void ToggleShowStats(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.ShowStats = menuItem.IsChecked;
    }

    private void ToggleShowToolbar(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.ShowToolbar = menuItem.IsChecked;
    }

    private void ToggleStat1(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.Stat1 = menuItem.IsChecked;
    }

    private void ToggleStat2(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.Stat2 = menuItem.IsChecked;
    }

    private void ToggleStat3(MenuItemData? menuItem)
    {
        if (menuItem is null)
        {
            return;
        }

        this.Stat3 = menuItem.IsChecked;
    }
}
