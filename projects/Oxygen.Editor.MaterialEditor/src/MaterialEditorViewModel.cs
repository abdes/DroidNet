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
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Import.Materials;
using Windows.ApplicationModel.DataTransfer;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// View model for the scalar material editor document.
/// </summary>
public sealed partial class MaterialEditorViewModel : ObservableObject, IAsyncSaveable, IDocumentCloseParticipant, IDisposable
{
    private readonly MaterialDocumentMetadata metadata;
    private readonly IMaterialDocumentService documentService;
    private readonly ILogger logger;
    private readonly Action<Uri>? assetChanged;
    private readonly SemaphoreSlim editGate = new(1, 1);
    private readonly Task loadTask;
    private Task<bool>? pendingSave;
    private MaterialDocument? document;
    private bool isLoading;
    private bool isDisposed;
    private bool isApplyingBaseColor;
    private bool isClosing;

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

        this.loadTask = this.LoadAsync();
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

    [ObservableProperty]
    public partial bool IsDirty { get; set; }

    [ObservableProperty]
    public partial string StatusText { get; set; } = "Not loaded";

    /// <summary>
    /// Gets the visibility of the dirty marker.
    /// </summary>
    public Visibility IsDirtyVisibility => this.IsDirty ? Visibility.Visible : Visibility.Collapsed;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        _ = this.DisposeGateAsync();
    }

    /// <inheritdoc />
    public async Task SaveAsync() => await this.SaveMaterialAsync().ConfigureAwait(true);

    /// <inheritdoc />
    public async Task PrepareForCloseAsync()
    {
        this.isClosing = true;
        await this.loadTask.ConfigureAwait(true);
        if (this.pendingSave is { } save)
        {
            _ = await save.ConfigureAwait(true);
        }

        await this.editGate.WaitAsync().ConfigureAwait(true);
        _ = this.editGate.Release();
    }

    /// <inheritdoc />
    public Task<bool> SaveForCloseAsync() => this.SaveCoreAsync();

    /// <inheritdoc />
    public async Task CloseAsync(bool discard)
    {
        await this.loadTask.ConfigureAwait(true);
        if (this.document is { } current)
        {
            await this.documentService.CloseAsync(current.DocumentId, discard).ConfigureAwait(true);
            this.document = null;
        }
    }

    /// <inheritdoc />
    public void ResumeEditing() => this.isClosing = false;

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
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.BaseColorR, r);
        edit.Set(MaterialDescriptors.BaseColorG, g);
        edit.Set(MaterialDescriptors.BaseColorB, b);
        edit.Set(MaterialDescriptors.BaseColorA, a);
        this.ApplyEdit(edit);
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(value * 255.0f), 0.0f, 255.0f);

    private static float ToFloat(byte value)
        => value / 255.0f;

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;

    private static string ToDisplayAlphaMode(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Mask => "Mask",
            MaterialAlphaMode.Blend => "Blend",
            _ => "Opaque",
        };

    private static MaterialAlphaMode ToMaterialAlphaMode(string value)
        => Enum.TryParse<MaterialAlphaMode>(value, ignoreCase: true, out var mode)
            ? mode
            : MaterialAlphaMode.Opaque;

    [LoggerMessage(EventId = 0, Level = LogLevel.Warning, Message = "Failed to open material document {MaterialUri}.")]
    private static partial void LogMaterialOpenFailed(ILogger logger, Exception exception, Uri materialUri);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Disposal drains pending work, logs its failure, and still releases the edit gate.")]
    private async Task DisposeGateAsync()
    {
        try
        {
            await this.loadTask.ConfigureAwait(true);
            if (this.pendingSave is { } save)
            {
                _ = await save.ConfigureAwait(true);
            }
        }
        catch (Exception exception)
        {
            this.LogPendingWorkFailedDuringDispose(exception);
        }

        await this.editGate.WaitAsync().ConfigureAwait(true);
        _ = this.editGate.Release();
        this.editGate.Dispose();
    }

    partial void OnBaseColorRChanged(float value) => this.ApplyColorEdit(PropertyEdit.Single(MaterialDescriptors.BaseColorR, value));

    partial void OnBaseColorGChanged(float value) => this.ApplyColorEdit(PropertyEdit.Single(MaterialDescriptors.BaseColorG, value));

    partial void OnBaseColorBChanged(float value) => this.ApplyColorEdit(PropertyEdit.Single(MaterialDescriptors.BaseColorB, value));

    partial void OnBaseColorAChanged(float value) => this.ApplyColorEdit(PropertyEdit.Single(MaterialDescriptors.BaseColorA, value));

    partial void OnMetallicFactorChanged(float value) => this.ApplyEdit(PropertyEdit.Single(MaterialDescriptors.Metalness, value));

    partial void OnRoughnessFactorChanged(float value) => this.ApplyEdit(PropertyEdit.Single(MaterialDescriptors.Roughness, value));

    partial void OnAlphaModeChanged(string value) => this.ApplyEdit(PropertyEdit.Single(MaterialDescriptors.AlphaMode, ToMaterialAlphaMode(value)));

    partial void OnAlphaCutoffChanged(float value) => this.ApplyEdit(PropertyEdit.Single(MaterialDescriptors.AlphaCutoff, value));

    partial void OnDoubleSidedChanged(bool value) => this.ApplyEdit(PropertyEdit.Single(MaterialDescriptors.DoubleSided, value));

    [RelayCommand]
    private async Task SaveMaterialAsync()
    {
        if (!this.isClosing && !this.isDisposed)
        {
            _ = await this.SaveCoreAsync().ConfigureAwait(true);
        }
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "Pending material work failed while disposing the editor.")]
    private partial void LogPendingWorkFailedDuringDispose(Exception exception);

    private Task<bool> SaveCoreAsync() => this.pendingSave = this.SaveAfterPendingAsync(this.pendingSave);

    private async Task<bool> SaveAfterPendingAsync(Task<bool>? previous)
    {
        if (previous is { IsCompleted: false })
        {
            _ = await previous.ConfigureAwait(true);
        }

        return await this.SaveSnapshotAsync().ConfigureAwait(true);
    }

    private async Task<bool> SaveSnapshotAsync()
    {
        await this.loadTask.ConfigureAwait(true);
        await this.editGate.WaitAsync().ConfigureAwait(true);
        var current = this.document;
        _ = this.editGate.Release();
        if (current is null)
        {
            this.StatusText = "The material is not loaded and cannot be saved.";
            return false;
        }

        var result = await this.documentService.SaveAsync(current.DocumentId).ConfigureAwait(true);
        if (this.isDisposed)
        {
            return result.Succeeded && !result.HasUnsavedChanges;
        }

        if (!result.Succeeded)
        {
            this.StatusText = "Save failed. Your changes are still open.";
            return false;
        }

        this.document = this.documentService.GetDocument(current.DocumentId);
        this.metadata.IsDirty = this.document.IsDirty;
        this.IsDirty = this.document.IsDirty;
        this.StatusText = this.IsDirty ? "Saved; newer changes remain unsaved" : "Saved";
        this.assetChanged?.Invoke(this.metadata.MaterialUri);
        return !this.IsDirty;
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
            LogMaterialOpenFailed(this.logger, ex, this.metadata.MaterialUri);
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

    private void ApplyColorEdit(PropertyEdit edit)
    {
        if (this.isApplyingBaseColor)
        {
            return;
        }

        this.OnPropertyChanged(nameof(this.BaseColorBrush));
        this.OnPropertyChanged(nameof(this.BaseColorColor));
        this.ApplyEdit(edit);
    }

    private void ApplyEdit(PropertyEdit edit)
    {
        if (this.isLoading || this.isClosing || this.isDisposed || this.document is null)
        {
            return;
        }

        _ = this.ApplyEditAsync(edit);
    }

    private async Task ApplyEditAsync(PropertyEdit edit)
    {
        var current = this.document;
        if (current is null)
        {
            return;
        }

        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var result = await this.documentService
                .EditPropertiesAsync(current.DocumentId, edit)
                .ConfigureAwait(true);
            if (result.Succeeded)
            {
                this.document = this.documentService.GetDocument(current.DocumentId);
                this.metadata.IsDirty = true;
                this.IsDirty = true;
                this.CookState = MaterialCookState.Stale;
                this.StatusText = "Unsaved changes";
            }
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    partial void OnIsDirtyChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.IsDirtyVisibility));
    }
}
