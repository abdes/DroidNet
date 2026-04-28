// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Oxygen.Assets.Import.Materials;
using Oxygen.Editor.ContentPipeline;
using Windows.ApplicationModel.DataTransfer;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// View model for the scalar material editor document.
/// </summary>
public sealed partial class MaterialEditorViewModel : ObservableObject, IAsyncSaveable, IDisposable
{
    private readonly MaterialDocumentMetadata metadata;
    private readonly IMaterialDocumentService documentService;
    private readonly ILogger logger;
    private readonly Action<Uri>? assetChanged;
    private readonly SemaphoreSlim editGate = new(1, 1);
    private MaterialDocument? document;
    private bool isLoading;
    private bool isDisposed;
    private bool isApplyingBaseColor;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditorViewModel"/> class.
    /// </summary>
    /// <param name="metadata">The material document metadata.</param>
    /// <param name="documentService">The material document service.</param>
    /// <param name="loggerFactory">Optional logger factory.</param>
    /// <param name="assetChanged">Optional callback used by the host to refresh content-browser projections.</param>
    public MaterialEditorViewModel(
        MaterialDocumentMetadata metadata,
        IMaterialDocumentService documentService,
        ILoggerFactory? loggerFactory = null,
        Action<Uri>? assetChanged = null)
    {
        this.metadata = metadata ?? throw new ArgumentNullException(nameof(metadata));
        this.documentService = documentService ?? throw new ArgumentNullException(nameof(documentService));
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<MaterialEditorViewModel>();
        this.assetChanged = assetChanged;
        this.MaterialUriText = metadata.MaterialUri.ToString();

        _ = this.LoadAsync();
    }

    /// <summary>
    /// Gets the available alpha modes.
    /// </summary>
    public IReadOnlyList<string> AlphaModes { get; } = ["Opaque", "Mask", "Blend"];

    /// <summary>
    /// Gets the color preview brush.
    /// </summary>
    public SolidColorBrush BaseColorBrush
        => new(Color.FromArgb(
            ToByte(this.BaseColorA),
            ToByte(this.BaseColorR),
            ToByte(this.BaseColorG),
            ToByte(this.BaseColorB)));

    /// <summary>
    /// Gets the current base color as a WinUI color for color-picker integration.
    /// </summary>
    public Color BaseColorColor
        => Color.FromArgb(
            ToByte(this.BaseColorA),
            ToByte(this.BaseColorR),
            ToByte(this.BaseColorG),
            ToByte(this.BaseColorB));

    [ObservableProperty]
    public partial string DisplayName { get; set; } = "Material";

    [ObservableProperty]
    public partial string MaterialUriText { get; set; }

    [ObservableProperty]
    public partial string MaterialGuidText { get; set; } = string.Empty;

    [ObservableProperty]
    public partial float BaseColorR { get; set; } = 1.0f;

    [ObservableProperty]
    public partial float BaseColorG { get; set; } = 1.0f;

    [ObservableProperty]
    public partial float BaseColorB { get; set; } = 1.0f;

    [ObservableProperty]
    public partial float BaseColorA { get; set; } = 1.0f;

    [ObservableProperty]
    public partial float MetallicFactor { get; set; }

    [ObservableProperty]
    public partial float RoughnessFactor { get; set; } = 0.5f;

    [ObservableProperty]
    public partial string AlphaMode { get; set; } = "Opaque";

    [ObservableProperty]
    public partial float AlphaCutoff { get; set; } = 0.5f;

    [ObservableProperty]
    public partial bool DoubleSided { get; set; }

    [ObservableProperty]
    public partial MaterialCookState CookState { get; set; } = MaterialCookState.NotCooked;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.editGate.Dispose();
        if (this.document is not null)
        {
            _ = this.documentService.CloseAsync(this.document.DocumentId, discard: true);
        }
    }

    /// <inheritdoc />
    public async Task SaveAsync() => await this.SaveMaterialAsync().ConfigureAwait(true);

    partial void OnBaseColorRChanged(float value) => this.ApplyColorEdit(MaterialFieldKeys.BaseColorR, value);

    partial void OnBaseColorGChanged(float value) => this.ApplyColorEdit(MaterialFieldKeys.BaseColorG, value);

    partial void OnBaseColorBChanged(float value) => this.ApplyColorEdit(MaterialFieldKeys.BaseColorB, value);

    partial void OnBaseColorAChanged(float value) => this.ApplyColorEdit(MaterialFieldKeys.BaseColorA, value);

    partial void OnMetallicFactorChanged(float value) => this.ApplyEdit(MaterialFieldKeys.MetallicFactor, value);

    partial void OnRoughnessFactorChanged(float value) => this.ApplyEdit(MaterialFieldKeys.RoughnessFactor, value);

    partial void OnAlphaModeChanged(string value) => this.ApplyEdit(MaterialFieldKeys.AlphaMode, value);

    partial void OnAlphaCutoffChanged(float value) => this.ApplyEdit(MaterialFieldKeys.AlphaCutoff, value);

    partial void OnDoubleSidedChanged(bool value) => this.ApplyEdit(MaterialFieldKeys.DoubleSided, value);

    /// <summary>
    /// Applies a picker-selected base color to the scalar descriptor channels.
    /// </summary>
    /// <param name="color">The selected color.</param>
    public void SetBaseColor(Color color)
    {
        var r = ToFloat(color.R);
        var g = ToFloat(color.G);
        var b = ToFloat(color.B);
        var a = ToFloat(color.A);
        if (NearlyEqual(this.BaseColorR, r)
            && NearlyEqual(this.BaseColorG, g)
            && NearlyEqual(this.BaseColorB, b)
            && NearlyEqual(this.BaseColorA, a))
        {
            return;
        }

        this.isApplyingBaseColor = true;
        try
        {
            this.BaseColorR = r;
            this.BaseColorG = g;
            this.BaseColorB = b;
            this.BaseColorA = a;
        }
        finally
        {
            this.isApplyingBaseColor = false;
        }

        this.OnPropertyChanged(nameof(this.BaseColorBrush));
        this.OnPropertyChanged(nameof(this.BaseColorColor));
        this.ApplyEdit(MaterialFieldKeys.BaseColorR, r);
        this.ApplyEdit(MaterialFieldKeys.BaseColorG, g);
        this.ApplyEdit(MaterialFieldKeys.BaseColorB, b);
        this.ApplyEdit(MaterialFieldKeys.BaseColorA, a);
    }

    [RelayCommand]
    private async Task SaveMaterialAsync()
    {
        if (this.document is null)
        {
            return;
        }

        var result = await this.documentService.SaveAsync(this.document.DocumentId).ConfigureAwait(true);
        if (result.Succeeded)
        {
            this.metadata.IsDirty = false;
            this.IsDirty = false;
            this.StatusText = "Saved";
            this.assetChanged?.Invoke(this.metadata.MaterialUri);
        }
    }

    [RelayCommand]
    private async Task CookAsync()
    {
        if (this.document is null)
        {
            return;
        }

        var result = await this.documentService.CookAsync(this.document.DocumentId).ConfigureAwait(true);
        this.CookState = result.State;
        this.StatusText = result.State == MaterialCookState.Rejected
            ? "Save the material before cooking."
            : $"Cook: {result.State}";
        if (result.State is MaterialCookState.Cooked or MaterialCookState.Stale)
        {
            this.assetChanged?.Invoke(this.metadata.MaterialUri);
        }
    }

    [RelayCommand]
    private void CopyMaterialUri()
        => this.CopyText(this.MaterialUriText, "Asset URI copied");

    [RelayCommand]
    private void CopyMaterialGuid()
        => this.CopyText(this.MaterialGuidText, "Asset GUID copied");

    private void CopyText(string text, string statusText)
    {
        var package = new DataPackage();
        package.SetText(text);
        Clipboard.SetContent(package);
        this.StatusText = statusText;
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(value * 255.0f), 0.0f, 255.0f);

    private static float ToFloat(byte value)
        => value / 255.0f;

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;

    private async Task LoadAsync()
    {
        try
        {
            this.isLoading = true;
            this.document = await this.documentService.OpenAsync(this.metadata.MaterialUri).ConfigureAwait(true);
            this.ReadFromDocument(this.document);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            this.logger.LogWarning(ex, "Failed to open material document {MaterialUri}.", this.metadata.MaterialUri);
        }
        finally
        {
            this.isLoading = false;
        }
    }

    private void ReadFromDocument(MaterialDocument value)
    {
        var source = value.Source;
        var pbr = source.PbrMetallicRoughness;

        this.DisplayName = value.DisplayName;
        this.MaterialGuidText = value.MaterialGuid.ToString("D");
        this.BaseColorR = pbr.BaseColorR;
        this.BaseColorG = pbr.BaseColorG;
        this.BaseColorB = pbr.BaseColorB;
        this.BaseColorA = pbr.BaseColorA;
        this.MetallicFactor = pbr.MetallicFactor;
        this.RoughnessFactor = pbr.RoughnessFactor;
        this.AlphaMode = ToDisplayAlphaMode(source.AlphaMode);
        this.AlphaCutoff = source.AlphaCutoff;
        this.DoubleSided = source.DoubleSided;
        this.CookState = value.CookState;
        this.IsDirty = value.IsDirty;
        this.StatusText = $"Cook: {value.CookState}";
        this.OnPropertyChanged(nameof(this.BaseColorBrush));
        this.OnPropertyChanged(nameof(this.BaseColorColor));
    }

    private static string ToDisplayAlphaMode(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Mask => "Mask",
            MaterialAlphaMode.Blend => "Blend",
            _ => "Opaque",
        };

    private void ApplyColorEdit(string fieldKey, float value)
    {
        if (this.isApplyingBaseColor)
        {
            return;
        }

        this.OnPropertyChanged(nameof(this.BaseColorBrush));
        this.OnPropertyChanged(nameof(this.BaseColorColor));
        this.ApplyEdit(fieldKey, value);
    }

    private void ApplyEdit(string fieldKey, object? value)
    {
        if (this.isLoading || this.document is null)
        {
            return;
        }

        _ = this.ApplyEditAsync(fieldKey, value);
    }

    private async Task ApplyEditAsync(string fieldKey, object? value)
    {
        if (this.document is null)
        {
            return;
        }

        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (this.document is null)
            {
                return;
            }

            var result = await this.documentService
                .EditScalarAsync(this.document.DocumentId, new MaterialFieldEdit(fieldKey, value))
                .ConfigureAwait(true);
            if (result.Succeeded)
            {
                this.metadata.IsDirty = true;
                this.IsDirty = true;
                this.CookState = MaterialCookState.Stale;
                this.StatusText = "Unsaved changes";
            }
        }
        finally
        {
            this.editGate.Release();
        }
    }

    [ObservableProperty]
    public partial bool IsDirty { get; set; }

    [ObservableProperty]
    public partial string StatusText { get; set; } = "Not loaded";

    public Visibility IsDirtyVisibility => this.IsDirty ? Visibility.Visible : Visibility.Collapsed;

    partial void OnIsDirtyChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.IsDirtyVisibility));
    }
}
