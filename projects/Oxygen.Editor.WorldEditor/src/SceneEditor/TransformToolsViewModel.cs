// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Workspace;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// The scene's viewport tools: which gizmo the selection shows (Select Q, Move W, Rotate E, Scale R),
/// and the transform space and snapping that every pane shares.
/// </summary>
/// <remarks>
/// The tool belongs to the editing session; space and snapping are per-user preferences kept by
/// <see cref="TransformToolSettingsService"/>.
/// </remarks>
public sealed partial class TransformToolsViewModel : ObservableObject, IDisposable
{
    private static readonly RuntimeTransformTool[] ToolCycle =
    [
        RuntimeTransformTool.Select,
        RuntimeTransformTool.Translate,
        RuntimeTransformTool.Rotate,
        RuntimeTransformTool.Scale,
    ];

    private bool isDisposed;

    /// <summary>Initializes a new instance of the <see cref="TransformToolsViewModel"/> class.</summary>
    /// <param name="settings">The shared space and snapping preferences.</param>
    public TransformToolsViewModel(TransformToolSettingsService settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        this.Settings = settings;
        this.TranslationOptions = [.. TransformToolSettingsService.TranslationPresets.Select(value => CreateOption(FormatTranslation(value), () => settings.TranslationIncrement = value))];
        this.RotationOptions = [.. TransformToolSettingsService.RotationPresets.Select(value => CreateOption(FormatRotation(value), () => settings.RotationIncrement = value))];
        this.ScaleOptions = [.. TransformToolSettingsService.ScalePresets.Select(value => CreateOption(FormatScale(value), () => settings.ScaleIncrement = value))];
        this.TranslationField = new ViewportCameraNumberBoxItemModel(
            settings.TranslationIncrement, 0.001F, TransformToolSettingsService.MaximumIncrement, "m", "~.###", onNumberValueChanged: value => settings.TranslationIncrement = value);
        this.RotationField = new ViewportCameraNumberBoxItemModel(
            settings.RotationIncrement, 0.01F, 360.0F, "°", "~.##", onNumberValueChanged: value => settings.RotationIncrement = value);
        this.ScaleField = new ViewportCameraNumberBoxItemModel(
            settings.ScaleIncrement, 0.001F, TransformToolSettingsService.MaximumIncrement, string.Empty, "~.###", onNumberValueChanged: value => settings.ScaleIncrement = value);
        settings.PropertyChanged += this.OnSettingsChanged;
        this.UpdateOptions();
    }

    /// <summary>Raised when the tool, space or snapping changed.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets or sets the tool whose gizmo the selection shows.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsSelectTool))]
    [NotifyPropertyChangedFor(nameof(IsMoveTool))]
    [NotifyPropertyChangedFor(nameof(IsRotateTool))]
    [NotifyPropertyChangedFor(nameof(IsScaleTool))]
    [NotifyPropertyChangedFor(nameof(SpaceToolTip))]
    public partial RuntimeTransformTool Tool { get; set; } = RuntimeTransformTool.Translate;

    /// <summary>Gets the shared space and snapping preferences.</summary>
    public TransformToolSettingsService Settings { get; }

    /// <summary>Gets a value indicating whether the Select tool is current.</summary>
    public bool IsSelectTool => this.Tool == RuntimeTransformTool.Select;

    /// <summary>Gets a value indicating whether the Move tool is current.</summary>
    public bool IsMoveTool => this.Tool == RuntimeTransformTool.Translate;

    /// <summary>Gets a value indicating whether the Rotate tool is current.</summary>
    public bool IsRotateTool => this.Tool == RuntimeTransformTool.Rotate;

    /// <summary>Gets a value indicating whether the Scale tool is current.</summary>
    public bool IsScaleTool => this.Tool == RuntimeTransformTool.Scale;

    /// <summary>Gets or sets a value indicating whether translate and rotate follow the active node's axes.</summary>
    public bool IsLocalSpace
    {
        get => this.Settings.Space == RuntimeTransformSpace.Local;
        set => this.Settings.Space = value ? RuntimeTransformSpace.Local : RuntimeTransformSpace.World;
    }

    /// <summary>Gets the transform space's short label.</summary>
    public string SpaceLabel => this.IsLocalSpace ? "Local" : "World";

    /// <summary>Gets the transform space's glyph.</summary>
    public string SpaceGlyph => this.IsLocalSpace ? "" : "";

    /// <summary>Gets the transform space toggle's tooltip, which notes that scale always uses local axes.</summary>
    public string SpaceToolTip => this.IsScaleTool
        ? $"Transform space: {this.SpaceLabel} (scale always uses the node's own axes)"
        : $"Transform space: {this.SpaceLabel}";

    /// <summary>Gets or sets a value indicating whether gizmo drags snap.</summary>
    public bool SnapEnabled
    {
        get => this.Settings.SnapEnabled;
        set => this.Settings.SnapEnabled = value;
    }

    /// <summary>Gets the snap toggle's tooltip.</summary>
    public string SnapToolTip => this.SnapEnabled
        ? "Snapping on (hold Ctrl while dragging to move freely)"
        : "Snapping off (hold Ctrl while dragging to snap)";

    /// <summary>Gets the translation increment's label.</summary>
    public string TranslationLabel => FormatTranslation(this.Settings.TranslationIncrement);

    /// <summary>Gets the rotation increment's label.</summary>
    public string RotationLabel => FormatRotation(this.Settings.RotationIncrement);

    /// <summary>Gets the scale increment's label.</summary>
    public string ScaleLabel => FormatScale(this.Settings.ScaleIncrement);

    /// <summary>Gets the translation increment presets.</summary>
    public IReadOnlyList<ViewportOption> TranslationOptions { get; }

    /// <summary>Gets the rotation increment presets.</summary>
    public IReadOnlyList<ViewportOption> RotationOptions { get; }

    /// <summary>Gets the scale increment presets.</summary>
    public IReadOnlyList<ViewportOption> ScaleOptions { get; }

    /// <summary>Gets the custom translation increment field.</summary>
    public ViewportCameraNumberBoxItemModel TranslationField { get; }

    /// <summary>Gets the custom rotation increment field.</summary>
    public ViewportCameraNumberBoxItemModel RotationField { get; }

    /// <summary>Gets the custom scale increment field.</summary>
    public ViewportCameraNumberBoxItemModel ScaleField { get; }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.Settings.PropertyChanged -= this.OnSettingsChanged;
    }

    /// <summary>Formats a translation increment, in metres.</summary>
    /// <param name="value">The increment.</param>
    /// <returns>The label, for example "0.25 m".</returns>
    internal static string FormatTranslation(float value) => string.Create(CultureInfo.InvariantCulture, $"{value:0.###} m");

    /// <summary>Formats a rotation increment, in degrees.</summary>
    /// <param name="value">The increment.</param>
    /// <returns>The label, for example "15°".</returns>
    internal static string FormatRotation(float value) => string.Create(CultureInfo.InvariantCulture, $"{value:0.##}°");

    /// <summary>Formats a scale increment.</summary>
    /// <param name="value">The increment.</param>
    /// <returns>The label, for example "0.1".</returns>
    internal static string FormatScale(float value) => value.ToString("0.###", CultureInfo.InvariantCulture);

    private static ViewportOption CreateOption(string label, Action apply)
        => new(label, description: null, () =>
        {
            apply();
            return Task.CompletedTask;
        });

    partial void OnToolChanged(RuntimeTransformTool value) => this.Changed?.Invoke(this, EventArgs.Empty);

    [RelayCommand]
    private void UseSelectTool() => this.Tool = RuntimeTransformTool.Select;

    [RelayCommand]
    private void UseMoveTool() => this.Tool = RuntimeTransformTool.Translate;

    [RelayCommand]
    private void UseRotateTool() => this.Tool = RuntimeTransformTool.Rotate;

    [RelayCommand]
    private void UseScaleTool() => this.Tool = RuntimeTransformTool.Scale;

    /// <summary>Moves to the next tool (Space): Select, Move, Rotate, Scale and around.</summary>
    [RelayCommand]
    private void CycleTool() => this.Tool = ToolCycle[(Array.IndexOf(ToolCycle, this.Tool) + 1) % ToolCycle.Length];

    [RelayCommand]
    private void ToggleSpace() => this.IsLocalSpace = !this.IsLocalSpace;

    [RelayCommand]
    private void ToggleSnap() => this.SnapEnabled = !this.SnapEnabled;

    private void OnSettingsChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(TransformToolSettingsService.Space):
                this.OnPropertyChanged(nameof(this.IsLocalSpace));
                this.OnPropertyChanged(nameof(this.SpaceLabel));
                this.OnPropertyChanged(nameof(this.SpaceGlyph));
                this.OnPropertyChanged(nameof(this.SpaceToolTip));
                break;
            case nameof(TransformToolSettingsService.SnapEnabled):
                this.OnPropertyChanged(nameof(this.SnapEnabled));
                this.OnPropertyChanged(nameof(this.SnapToolTip));
                break;
            case nameof(TransformToolSettingsService.TranslationIncrement):
            case nameof(TransformToolSettingsService.RotationIncrement):
            case nameof(TransformToolSettingsService.ScaleIncrement):
                this.UpdateOptions();
                break;
            default:
                return;
        }

        this.Changed?.Invoke(this, EventArgs.Empty);
    }

    private void UpdateOptions()
    {
        Mark(this.TranslationOptions, TransformToolSettingsService.TranslationPresets, this.Settings.TranslationIncrement);
        Mark(this.RotationOptions, TransformToolSettingsService.RotationPresets, this.Settings.RotationIncrement);
        Mark(this.ScaleOptions, TransformToolSettingsService.ScalePresets, this.Settings.ScaleIncrement);
        this.TranslationField.NumberValue = this.Settings.TranslationIncrement;
        this.RotationField.NumberValue = this.Settings.RotationIncrement;
        this.ScaleField.NumberValue = this.Settings.ScaleIncrement;
        this.OnPropertyChanged(nameof(this.TranslationLabel));
        this.OnPropertyChanged(nameof(this.RotationLabel));
        this.OnPropertyChanged(nameof(this.ScaleLabel));

        static void Mark(IReadOnlyList<ViewportOption> options, IReadOnlyList<float> presets, float value)
        {
            for (var i = 0; i < options.Count; i++)
            {
                options[i].IsSelected = presets[i] == value;
            }
        }
    }
}
