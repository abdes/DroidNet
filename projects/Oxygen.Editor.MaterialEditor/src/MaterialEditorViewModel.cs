// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Security.Cryptography;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Materials.Documents;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;
using Windows.ApplicationModel.DataTransfer;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// View model for the editor of a material document. It edits the document's <see cref="MaterialContent"/> through
/// <see cref="MaterialEdits"/>, and follows the document's state, reloads and relocations.
/// </summary>
/// <remarks>
/// The editor belongs to the project session; the window that shows its material lends it the window's services, which
/// its cook asks in (<see cref="IDocumentWindowEditor"/>).
/// </remarks>
public sealed partial class MaterialEditorViewModel : ObservableObject, IAsyncSaveable, IDocumentCloseParticipant, IDocumentWindowEditor, IDisposable
{
    private static readonly TimeSpan WheelIdleDelay = TimeSpan.FromMilliseconds(250);
    private readonly DocumentTab tab;
    private readonly Document document;
    private readonly MaterialEdits edits;
    private readonly IMaterialCookService cookService;
    private readonly IProjectSession session;
    private readonly ILogger logger;
    private readonly IAssetFeed assets;
    private readonly IDocumentInputCommitter? inputCommitter;
    private readonly IDocumentService? documents;
    private readonly IUnsavedCookInputs? unsavedInputs;
    private readonly SemaphoreSlim editGate = new(1, 1);
    private DocumentWindowServices? window;
    private Task<bool>? pendingSave;
    private MaterialContent? observedContent;
    private bool isApplyingOwnEdit;
    private bool isLoading;
    private bool isDisposed;
    private bool isApplyingBaseColor;
    private bool isClosing;
    private IReadOnlyList<AssetEntry> availableTextures = [];
    private double normalScale = 1.0;
    private double occlusionStrength = 1.0;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditorViewModel"/> class.
    /// </summary>
    /// <param name="tab">The material document's tab.</param>
    /// <param name="edits">Edits material documents and records their history.</param>
    /// <param name="assetProvider">The shared source, publication and cooking status feed.</param>
    /// <param name="uiScheduler">Marshals status updates to the document's UI thread.</param>
    /// <param name="cookService">Cooks the material on request.</param>
    /// <param name="session">The project session the editor lives in, whose project locates the material's file for a cook or a copy and holds the
    /// textures, read to offer only those with their images.</param>
    /// <param name="loggerFactory">Optional logger factory.</param>
    /// <param name="inputCommitter">Completes the focused numeric control before snapshot capture.</param>
    /// <param name="documents">Saves the unsaved documents a cook reads, after asking; without it a cook does not ask.</param>
    /// <param name="unsavedInputs">Finds the unsaved documents a cook reads; without it a cook does not ask.</param>
    public MaterialEditorViewModel(
        DocumentTab tab,
        MaterialEdits edits,
        IAssetFeed assetProvider,
        IScheduler uiScheduler,
        IMaterialCookService cookService,
        IProjectSession session,
        ILoggerFactory? loggerFactory = null,
        IDocumentInputCommitter? inputCommitter = null,
        IDocumentService? documents = null,
        IUnsavedCookInputs? unsavedInputs = null)
    {
        this.documents = documents;
        this.unsavedInputs = unsavedInputs;
        this.tab = tab ?? throw new ArgumentNullException(nameof(tab));
        this.document = tab.Document.Content is MaterialContent
            ? tab.Document
            : throw new ArgumentException($"'{tab.Title}' is not a material document.", nameof(tab));
        this.edits = edits ?? throw new ArgumentNullException(nameof(edits));
        this.cookService = cookService ?? throw new ArgumentNullException(nameof(cookService));
        this.session = session ?? throw new ArgumentNullException(nameof(session));
        this.logger = (loggerFactory ?? NullLoggerFactory.Instance).CreateLogger<MaterialEditorViewModel>();
        this.assets = assetProvider ?? throw new ArgumentNullException(nameof(assetProvider));
        this.inputCommitter = inputCommitter;
        this.MaterialUriText = this.document.File.Asset.ToUriString();
        this.TextureChannels =
        [
            new("base_color", "Base color", this.OnTextureSelectionChanged),
            new("normal", "Normal", this.OnTextureSelectionChanged),
            new("metallic", "Metallic", this.OnTextureSelectionChanged),
            new("roughness", "Roughness", this.OnTextureSelectionChanged),
            new("ambient_occlusion", "Ambient occlusion", this.OnTextureSelectionChanged),
            new("emissive", "Emissive", this.OnTextureSelectionChanged),
            new("specular", "Specular", this.OnTextureSelectionChanged),
            new("sheen_color", "Sheen color", this.OnTextureSelectionChanged),
            new("clearcoat", "Clearcoat", this.OnTextureSelectionChanged),
            new("clearcoat_normal", "Clearcoat normal", this.OnTextureSelectionChanged),
            new("transmission", "Transmission", this.OnTextureSelectionChanged),
            new("thickness", "Thickness", this.OnTextureSelectionChanged),
        ];

        this.assetStatusSubscription = assetProvider.Items.ObserveOn(uiScheduler)
            .Subscribe(this.ApplyAssetItems, this.OnAssetStatusFailed);
        this.assetStatusRefresh = this.RefreshAssetStatusAsync(assetProvider, this.assetStatusLifetime.Token);
        this.document.StateChanged += this.OnDocumentStateChanged;
        this.document.ContentReplaced += this.OnDocumentContentReplaced;
        this.ObserveContent();
        this.ReadFromContent();
    }

    /// <summary>
    /// Gets the available alpha modes.
    /// </summary>
    public IReadOnlyList<string> AlphaModes { get; } = ["Opaque", "Mask", "Blend"];

    /// <summary>Gets all native material texture channels and their current assignment choices.</summary>
    public IReadOnlyList<MaterialTextureChannel> TextureChannels { get; }

    /// <summary>Gets a value indicating whether the editor shows the document's authored values.</summary>
    public bool IsLoaded => !this.isLoading;

    /// <summary>Gets the linear base color factor: red, green, blue and alpha.</summary>
    public ColorChannels BaseColor => new(this.BaseColorR, this.BaseColorG, this.BaseColorB, this.BaseColorA);

    /// <summary>Gets the opaque display preview of the linear base color.</summary>
    public SolidColorBrush BaseColorBrush
        => new(ColorPresentation.ToDisplayColor(this.BaseColor with { Alpha = 1 }, ColorEncoding.Linear, ColorRange.Normalized));

    [ObservableProperty]
    public partial string DisplayName { get; set; } = "Material";

    [ObservableProperty]
    public partial string MaterialUriText { get; set; }

    [ObservableProperty]
    public partial string MaterialGuidText { get; set; } = string.Empty;

    [ObservableProperty]
    public partial double BaseColorR { get; set; } = 1.0;

    [ObservableProperty]
    public partial double BaseColorG { get; set; } = 1.0;

    [ObservableProperty]
    public partial double BaseColorB { get; set; } = 1.0;

    [ObservableProperty]
    public partial double BaseColorA { get; set; } = 1.0;

    [ObservableProperty]
    public partial double MetallicFactor { get; set; }

    [ObservableProperty]
    public partial double RoughnessFactor { get; set; } = 0.5;

    [ObservableProperty]
    public partial string AlphaMode { get; set; } = "Opaque";

    [ObservableProperty]
    public partial double AlphaCutoff { get; set; } = 0.5;

    [ObservableProperty]
    public partial bool DoubleSided { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CookStatusText))]
    [NotifyPropertyChangedFor(nameof(CookStatusTone))]
    [NotifyPropertyChangedFor(nameof(CookStatusDescription))]
    public partial bool IsDirty { get; set; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(StatusMessageVisibility))]
    public partial string StatusText { get; set; } = string.Empty;

    /// <summary>Gets the visibility of an actionable document message or command acknowledgement.</summary>
    public Visibility StatusMessageVisibility => string.IsNullOrEmpty(this.StatusText) ? Visibility.Collapsed : Visibility.Visible;

    /// <summary>Gets or sets the normal-map scale.</summary>
    public double NormalScale
    {
        get => this.normalScale;
        set
        {
            if (this.SetProperty(ref this.normalScale, value))
            {
                this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.NormalScale, (float)value));
            }
        }
    }

    /// <summary>Gets or sets the occlusion texture strength.</summary>
    public double OcclusionStrength
    {
        get => this.occlusionStrength;
        set
        {
            if (this.SetProperty(ref this.occlusionStrength, value))
            {
                this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.AmbientOcclusion, (float)value));
            }
        }
    }

    /// <summary>
    /// Applies a picked base color to the scalar descriptor channels as one edit.
    /// </summary>
    /// <param name="color">The linear red, green, blue and alpha channels.</param>
    public void SetBaseColor(ColorChannels color)
    {
        var (r, g, b, a) = (color.Red, color.Green, color.Blue, color.Alpha);
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
        var edit = new PropertyEdit.Builder()
            .Set(MaterialDescriptors.BaseColorR, (float)r)
            .Set(MaterialDescriptors.BaseColorG, (float)g)
            .Set(MaterialDescriptors.BaseColorB, (float)b)
            .Set(MaterialDescriptors.BaseColorA, (float)a)
            .Build();
        this.ApplyEdit(edit);
    }

    /// <inheritdoc />
    public void AttachWindow(DocumentWindowServices window)
    {
        ArgumentNullException.ThrowIfNull(window);
        this.window = window;
    }

    /// <inheritdoc />
    public void DetachWindow() => this.window = null;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.document.StateChanged -= this.OnDocumentStateChanged;
        this.document.ContentReplaced -= this.OnDocumentContentReplaced;
        this.ObserveContent(content: null);
        this.assetStatusSubscription.Dispose();
        this.assetStatusLifetime.Cancel();
        this.EndEditSession(NumberBoxEditCompletionKind.Cancel);
        _ = this.DisposeGateAsync();
    }

    /// <inheritdoc />
    public async Task SaveAsync() => await this.SaveMaterialAsync().ConfigureAwait(true);

    /// <inheritdoc />
    public async Task PrepareForCloseAsync()
    {
        if (this.inputCommitter is not null)
        {
            await this.inputCommitter.CommitAsync(this.tab.DocumentId).ConfigureAwait(true);
        }

        this.EndEditSession(NumberBoxEditCompletionKind.Commit);
        this.isClosing = true;
        if (this.pendingSave is { } save)
        {
            _ = await save.ConfigureAwait(true);
        }

        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        _ = this.editGate.Release();
        this.RefreshHistoryCommands();
    }

    /// <inheritdoc />
    public Task<bool> SaveForCloseAsync() => this.SaveCoreAsync();

    /// <inheritdoc />
    /// <remarks>The document area closes the document once its tab has closed; a discarded gesture ends here.</remarks>
    public Task CloseAsync(bool discard)
    {
        this.EndEditSession(discard ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);
        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public void ResumeEditing()
    {
        this.isClosing = false;
        this.RefreshHistoryCommands();
    }

    // A material's GUID is derived from its asset URI, as the cooker derives it.
    private static Guid GuidOf(AssetPath material)
    {
        var text = material.ToUriString().ToUpperInvariant();
        Span<byte> hash = stackalloc byte[32];
        _ = SHA256.HashData(Encoding.UTF8.GetBytes(text), hash);

        Span<byte> bytes = stackalloc byte[16];
        hash[..16].CopyTo(bytes);

        bytes[7] = (byte)((bytes[7] & 0x0F) | 0x50);
        bytes[8] = (byte)((bytes[8] & 0x3F) | 0x80);
        return new Guid(bytes);
    }

    private static bool NearlyEqual(double left, double right)
        => Math.Abs(left - right) <= 0.0001;

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

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Disposal drains pending work, logs its failure, and still releases the edit gate.")]
    private async Task DisposeGateAsync()
    {
        try
        {
            await this.assetStatusRefresh.ConfigureAwait(true);
            if (this.pendingSave is { } save)
            {
                _ = await save.ConfigureAwait(true);
            }
        }
        catch (Exception exception)
        {
            this.LogPendingWorkFailedDuringDispose(exception);
        }

        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        _ = this.editGate.Release();
        this.editGate.Dispose();
        this.assetStatusLifetime.Dispose();
    }

    partial void OnBaseColorRChanged(double value) => this.ApplyColorEdit(PropertyEdit.SingleEdit(MaterialDescriptors.BaseColorR, (float)value));

    partial void OnBaseColorGChanged(double value) => this.ApplyColorEdit(PropertyEdit.SingleEdit(MaterialDescriptors.BaseColorG, (float)value));

    partial void OnBaseColorBChanged(double value) => this.ApplyColorEdit(PropertyEdit.SingleEdit(MaterialDescriptors.BaseColorB, (float)value));

    partial void OnBaseColorAChanged(double value) => this.ApplyColorEdit(PropertyEdit.SingleEdit(MaterialDescriptors.BaseColorA, (float)value));

    partial void OnMetallicFactorChanged(double value) => this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.Metalness, (float)value));

    partial void OnRoughnessFactorChanged(double value) => this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.Roughness, (float)value));

    partial void OnAlphaModeChanged(string value) => this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.AlphaMode, ToMaterialAlphaMode(value)));

    partial void OnAlphaCutoffChanged(double value) => this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.AlphaCutoff, (float)value));

    partial void OnDoubleSidedChanged(bool value) => this.ApplyEdit(PropertyEdit.SingleEdit(MaterialDescriptors.DoubleSided, value));

    [RelayCommand]
    private async Task SaveMaterialAsync()
    {
        if (!this.isClosing && !this.isDisposed)
        {
            // A save that meets a newer file leaves the document changed on disk; the document area's banner offers the way out.
            _ = await this.SaveCoreAsync().ConfigureAwait(true);
        }
    }

    private void OnTextureSelectionChanged(MaterialTextureChannel channel, AssetPath? texture)
    {
        if (!this.isLoading && !this.isClosing && !this.isDisposed && this.acceptsInput)
        {
            _ = this.ApplyTextureReferenceAsync(channel, texture);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase", Justification = "the channel name is lowered for display in a sentence, not normalized for comparison")]
    private async Task ApplyTextureReferenceAsync(MaterialTextureChannel channel, AssetPath? texture)
    {
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            if (this.isDisposed)
            {
                return;
            }

            var result = this.ApplyOwnEdit(() => this.edits.SetTexture(this.document, channel.Channel, texture));
            this.RefreshFromDocument();
            this.StatusText = result.Succeeded
                ? string.Empty
                : $"The {channel.DisplayName.ToLowerInvariant()} texture change was rejected.";
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

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
        if (this.inputCommitter is not null)
        {
            await this.inputCommitter.CommitAsync(this.tab.DocumentId).ConfigureAwait(true);
        }

        this.EndEditSession(NumberBoxEditCompletionKind.Commit);
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        _ = this.editGate.Release();
        var result = await this.document.SaveAsync(CancellationToken.None).ConfigureAwait(true);
        if (this.isDisposed)
        {
            return result.Succeeded && !this.document.IsDirty;
        }

        if (!result.Succeeded)
        {
            this.StatusText = result.Outcome switch
            {
                DocumentSaveOutcome.Conflict => "The file changed outside the editor. Reload it, keep your changes or save a copy.",
                _ => $"Save failed. Your changes are still open. {result.Message}",
            };
            return false;
        }

        this.RefreshFromDocument();
        this.StatusText = this.IsDirty ? "Saved; newer changes remain unsaved" : string.Empty;
        return !this.IsDirty;
    }

    // The cook reads the saved file, so an unsaved material is saved first, after asking; the catalog status shows the
    // cook's result.
    [RelayCommand]
    private async Task CookAsync()
    {
        var project = this.session.Context;
        if (project.FindSource(this.document.File.Asset) is not { IsMountRoot: false } location)
        {
            this.StatusText = "The material is not in the active project and cannot be cooked.";
            return;
        }

        if (this.documents is { } documents && this.unsavedInputs is { } inputs)
        {
            // Without a window that shows the material, nobody can be asked to save what the cook reads.
            var request = new CookRunRequest(CookTargetKind.Asset, this.document.File.Asset);
            if (this.window is not { } window
                || !await documents.SaveBeforeAsync(window.Window, SaveBeforeOperation.Cook, token => inputs.FindAsync(request, token)).ConfigureAwait(true))
            {
                return;
            }
        }

        // An authoring mount's file is named from the project root; a local folder mount's file from its mount.
        var relativePath = location.MountKind == ProjectMountKind.Authoring
            ? location.File.GetPathBelow(project.ProjectRoot)
            : location.Path.RelativePath;
        _ = await this.cookService.CookMaterialAsync(
            new MaterialCookRequest(this.document.File.Asset, project.ProjectRoot, location.Path.Mount, relativePath),
            CancellationToken.None).ConfigureAwait(true);
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

    private void ReadFromContent()
    {
        this.isLoading = true;
        try
        {
            var source = ((MaterialContent)this.document.Content).Material.Source;
            var pbr = source.PbrMetallicRoughness;

            this.DisplayName = this.document.Title;
            this.MaterialUriText = this.document.File.Asset.ToUriString();
            this.MaterialGuidText = GuidOf(this.document.File.Asset).ToString("D");
            this.BaseColorR = pbr.BaseColorR;
            this.BaseColorG = pbr.BaseColorG;
            this.BaseColorB = pbr.BaseColorB;
            this.BaseColorA = pbr.BaseColorA;
            this.MetallicFactor = pbr.MetallicFactor;
            this.RoughnessFactor = pbr.RoughnessFactor;
            this.NormalScale = source.NormalScale;
            this.OcclusionStrength = source.OcclusionStrength;
            this.AlphaMode = ToDisplayAlphaMode(source.AlphaMode);
            this.AlphaCutoff = source.AlphaCutoff;
            this.DoubleSided = source.DoubleSided;
            foreach (var channel in this.TextureChannels)
            {
                _ = source.TextureReferences.TryGetValue(channel.Channel, out var texture);
                channel.Refresh(this.availableTextures, texture);
            }

            this.IsDirty = this.document.IsDirty;
            this.OnPropertyChanged(nameof(this.BaseColorBrush));
        }
        finally
        {
            this.isLoading = false;
            this.OnPropertyChanged(nameof(this.IsLoaded));
        }

        this.RefreshHistoryCommands();
    }

    private void ApplyColorEdit(PropertyEdit edit)
    {
        if (this.isApplyingBaseColor)
        {
            return;
        }

        this.OnPropertyChanged(nameof(this.BaseColorBrush));
        this.ApplyEdit(edit);
    }

    private void ApplyEdit(PropertyEdit edit)
    {
        if (this.isLoading || this.isClosing || this.isDisposed || !this.acceptsInput)
        {
            return;
        }

        _ = this.ApplyEditAsync(edit, this.activeGesture);
    }

    private async Task ApplyEditAsync(PropertyEdit edit, EditGesture? gesture)
    {
        await this.editGate.WaitAsync(CancellationToken.None).ConfigureAwait(true);
        try
        {
            if (this.isDisposed)
            {
                return;
            }

            var result = this.ApplyOwnEdit(() =>
            {
                if (gesture is null)
                {
                    return this.edits.Apply(this.document, edit);
                }

                gesture.Session ??= this.edits.BeginEditSession(this.document, gesture.Field);
                return this.edits.Preview(this.document, gesture.Session.Value, edit);
            });

            // A rejected edit shows the values the material kept.
            this.RefreshFromDocument(refreshValues: !result.Succeeded);
            this.StatusText = result.Succeeded
                ? string.Empty
                : "The value was rejected. The previous value has been restored.";
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }
}
