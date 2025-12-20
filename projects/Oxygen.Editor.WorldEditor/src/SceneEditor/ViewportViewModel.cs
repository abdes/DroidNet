// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
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
using Oxygen.Editor.Documents;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Interop;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// ViewModel for the Viewport control, managing camera, shading, and menus.
/// </summary>
public partial class ViewportViewModel : ObservableObject, IDisposable
{
    private readonly ILogger logger;
    private readonly ISettingsService<IAppearanceSettings> appearanceSettings;
    private IMenuSource? viewMenu;
    private IMenuSource? shadingMenu;
    private IMenuSource? layoutMenu;
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
        this.LoggerFactory = loggerFactory;
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger("Oxygen.Editor.LevelEditor.ViewportViewModel");

        this.DocumentId = documentId;
        this.EngineService = engineService;
        this.appearanceSettings = appearanceSettings;

        // Seed effective theme from settings and subscribe for changes.
        this.SetEffectiveTheme(this.appearanceSettings.Settings.AppThemeMode);
        this.appearanceSettings.PropertyChanged += this.AppearanceSettings_PropertyChanged;
        this.ToggleMaximizeCommand = new RelayCommand(() => this.IsMaximized = !this.IsMaximized);
        this.LogInitialized();
    }

    // Overlay view toggles
    [ObservableProperty]
    public partial bool ShowFps { get; set; }

    [ObservableProperty]
    public partial bool ShowStats { get; set; }

    [ObservableProperty]
    public partial bool ShowToolbar { get; set; }

    [ObservableProperty]
    public partial bool Stat1 { get; set; }

    [ObservableProperty]
    public partial bool Stat2 { get; set; }

    [ObservableProperty]
    public partial bool Stat3 { get; set; }

    [ObservableProperty]
    public partial CameraType CameraType { get; set; } = CameraType.Perspective;

    [ObservableProperty]
    public partial ShadingMode ShadingMode { get; set; } = ShadingMode.Wireframe;

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
    /// Gets or sets if the UI requested an engine view for this viewport, the engine-assigned
    /// identifier will be stored here so the view can be destroyed later during
    /// teardown. Managed and owned by the UI layer — engine surface/lease code is
    /// unaffected by this property.
    /// </summary>
    public ViewIdManaged AssignedViewId { get; set; } = ViewIdManaged.Invalid;

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

    // Background clear color used when creating a matching engine view. Exposed
    // from the view-model so each viewport can choose its own diagnostic tint.
    [ObservableProperty]
    public partial ColorManaged ClearColor { get; set; } = new ColorManaged(0.1f, 0.12f, 0.15f, 1.0f);

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
        this.Dispose(disposing: true);
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
    /// Set the effective theme used for selecting themed resources (Light/Dark).
    /// Call this from the view (code-behind) using the view's ActualTheme.
    /// </summary>
    /// <param name="theme">The theme reported by the view (ActualTheme).</param>
    public void UpdateTheme(ElementTheme theme) => this.SetEffectiveTheme(theme);

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

    private static MenuItemData CreateToggleMenuItem(string text, Func<bool> getter, Action<bool> setter, string? accelerator = null)
        => new()
        {
            Text = text,
            IsCheckable = true,
            IsChecked = getter(),
            AcceleratorText = accelerator,
            Command = new RelayCommand<MenuItemData?>(item =>
            {
                if (item is null)
                {
                    return;
                }

                setter(item.IsChecked);
            }),
        };

    partial void OnIsMaximizedChanged(bool oldValue, bool newValue)
    {
        // keep MaximizeGlyph synched with IsMaximized
        this.OnPropertyChanged(nameof(this.MaximizeGlyph));
    }

    private void SetEffectiveTheme(ElementTheme theme)
    {
        var hadViewMenu = this.viewMenu is not null;
        var hadShadingMenu = this.shadingMenu is not null;
        var hadLayoutMenu = this.layoutMenu is not null;

        this.effectiveThemeOverride = theme;
        this.LogEffectiveThemeSet(theme);

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
        }
        catch (Exception ex)
        {
            this.LogMenuRebuildFailed(ex);
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
        if (string.IsNullOrWhiteSpace(name))
        {
            this.LogResolveIconEmptyName();
            return null;
        }

        if (!this.effectiveThemeOverride.HasValue)
        {
            this.LogResolveIconBeforeTheme();
            return null;
        }

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
            .AddSeparator();

        foreach (var type in new[] { CameraType.Top, CameraType.Bottom, CameraType.Left, CameraType.Right, CameraType.Front, CameraType.Back })
        {
            _ = builder.AddRadioMenuItem(type.ToString(), "CameraType", this.CameraType == type, new RelayCommand(() => this.CameraType = type));
        }

        return builder.Build();
    }

    private IMenuSource BuildShadingMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);

        foreach (var mode in new[] { ShadingMode.Wireframe, ShadingMode.Shaded, ShadingMode.Rendered })
        {
            _ = builder.AddRadioMenuItem(mode.ToString(), "ShadingMode", this.ShadingMode == mode, new RelayCommand(() => this.ShadingMode = mode));
        }

        return builder.Build();
    }

    private IMenuSource BuildLayoutMenu()
    {
        var builder = new MenuBuilder(this.LoggerFactory);
        _ = builder.AddMenuItem(CreateToggleMenuItem("Show FPS", () => this.ShowFps, v => this.ShowFps = v, "Ctrl+Shift+H"))
            .AddMenuItem(CreateToggleMenuItem("Show Stats", () => this.ShowStats, v => this.ShowStats = v, "Shift+L"))
            .AddSubmenu("Stats", submenu => submenu
                .AddMenuItem(CreateToggleMenuItem("Stat1", () => this.Stat1, v => this.Stat1 = v))
                .AddMenuItem(CreateToggleMenuItem("Stat2", () => this.Stat2, v => this.Stat2 = v))
                .AddMenuItem(CreateToggleMenuItem("Stat3", () => this.Stat3, v => this.Stat3 = v)))
            .AddMenuItem(CreateToggleMenuItem("Show Toolbar", () => this.ShowToolbar, v => this.ShowToolbar = v, "Ctrl+Shift+T"))
            .AddSeparator();

        // Layouts submenu with grouped panes and themed icons
        _ = builder.AddSubmenu("Layouts", layouts =>
        {
            _ = layouts.AddMenuItem("One Pane", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.OnePane)), this.ResolveIcon("OnePane"));
            _ = layouts.AddMenuItem("Four Quadrants", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourQuad)), this.ResolveIcon("FourQuad"));
            _ = layouts.AddSubmenu("Two Panes", two =>
            {
                _ = two.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainLeft)), this.ResolveIcon("TwoMainLeft"));
                _ = two.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainRight)), this.ResolveIcon("TwoMainRight"));
                _ = two.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainTop)), this.ResolveIcon("TwoMainTop"));
                _ = two.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.TwoMainBottom)), this.ResolveIcon("TwoMainBottom"));
            });
            _ = layouts.AddSubmenu("Three Panes", three =>
            {
                _ = three.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainLeft)), this.ResolveIcon("ThreeMainLeft"));
                _ = three.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainRight)), this.ResolveIcon("ThreeMainRight"));
                _ = three.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainTop)), this.ResolveIcon("ThreeMainTop"));
                _ = three.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.ThreeMainBottom)), this.ResolveIcon("ThreeMainBottom"));
            });
            _ = layouts.AddSubmenu("Four Panes", four =>
            {
                _ = four.AddMenuItem("Main Left", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainLeft)), this.ResolveIcon("FourMainLeft"));
                _ = four.AddMenuItem("Main Right", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainRight)), this.ResolveIcon("FourMainRight"));
                _ = four.AddMenuItem("Main Top", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainTop)), this.ResolveIcon("FourMainTop"));
                _ = four.AddMenuItem("Main Bottom", new RelayCommand(() => this.OnLayoutRequested?.Invoke(SceneViewLayout.FourMainBottom)), this.ResolveIcon("FourMainBottom"));
            });
        });

        return builder.Build();
    }
}
