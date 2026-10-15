// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Observes the same asset facts as the browser and typed picker, independently of document edits.</summary>
public sealed partial class MaterialEditorViewModel
{
    private readonly CancellationTokenSource assetStatusLifetime = new();
    private readonly IDisposable assetStatusSubscription;
    private readonly Task assetStatusRefresh;

    /// <summary>Gets or sets the current shared asset presentation.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CookStatusText))]
    [NotifyPropertyChangedFor(nameof(CookStatusTone))]
    [NotifyPropertyChangedFor(nameof(CookStatusDescription))]
    public partial AssetEntry? AssetStatus { get; set; }

    /// <summary>Gets or sets whether the initial shared status query is still pending.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CookStatusText))]
    public partial bool IsCheckingCookStatus { get; set; } = true;

    /// <summary>Gets the concise source and cook status, including edits newer than the published content.</summary>
    public string CookStatusText => AssetStatusText.GetText(this.AssetStatus?.CookStatus, this.AssetStatus?.CookActivity, this.IsDirty, this.AssetStatus?.RuntimeAvailability ?? AssetRuntimeAvailability.Unknown)
        ?? (this.IsCheckingCookStatus ? "Checking status" : "Status unavailable");

    /// <summary>Gets the semantic theme state for the compact cook-status chip.</summary>
    public string CookStatusTone => AssetStatusText.GetTone(this.AssetStatus?.CookStatus, this.AssetStatus?.CookActivity, this.IsDirty, this.AssetStatus?.RuntimeAvailability ?? AssetRuntimeAvailability.Unknown);

    /// <summary>Gets the next action and previous-output facts for the status tooltip.</summary>
    public string CookStatusDescription => AssetStatusText.GetDescription(this.AssetStatus?.CookStatus, this.AssetStatus?.CookActivity, this.IsDirty, this.AssetStatus?.RuntimeAvailability ?? AssetRuntimeAvailability.Unknown, this.AssetStatus?.RuntimeReason);

    private void ApplyAssetItems(IReadOnlyList<AssetEntry> items)
    {
        if (!this.isDisposed)
        {
            this.AssetStatus = items.FirstOrDefault(item => item.IdentityUri == this.metadata.MaterialUri || item.CookedUri == this.metadata.MaterialUri);
            this.availableTextureChoices = AssetRule.For(AssetKind.Texture).Apply(items)
                .Where(static item => item.DescriptorPath is { } descriptorPath
                    && item.IdentityUri.AbsolutePath.EndsWith(AssetFileTypes.TextureDescriptor, StringComparison.OrdinalIgnoreCase)
                    && TextureSourceAssetImporter.HasSourceImage(descriptorPath))
                .Select(static item => new MaterialTextureChoice(
                    Uri.UnescapeDataString(item.IdentityUri.AbsolutePath)[..^AssetFileTypes.DescriptorSuffix.Length],
                    item.DisplayName,
                    IsAvailable: true))
                .DistinctBy(static choice => choice.VirtualPath, StringComparer.Ordinal)
                .ToArray();
            foreach (var channel in this.TextureChannels)
            {
                channel.Refresh(this.availableTextureChoices, channel.SelectedVirtualPath);
            }

            // The feed republishes when file watching sees the material removed or restored outside the editor.
            _ = this.RefreshSourcePresenceAsync();
        }
    }

    private async Task RefreshAssetStatusAsync(IAssetFeed provider, CancellationToken cancellationToken)
    {
        try
        {
            await provider.RefreshAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Closing this document detaches its wait without cancelling the workspace's shared scan.
        }
        catch (Exception exception) when (exception is IOException or InvalidDataException or UnauthorizedAccessException or ArgumentException or NotSupportedException or ObjectDisposedException)
        {
            this.OnAssetStatusFailed(exception);
        }
        finally
        {
            if (!this.isDisposed)
            {
                this.IsCheckingCookStatus = false;
            }
        }
    }

    private void OnAssetStatusFailed(Exception exception)
    {
        if (!this.isDisposed)
        {
            this.AssetStatus = null;
            this.LogAssetStatusFailed(exception, this.metadata.MaterialUri);
        }
    }

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not refresh shared asset status for material {MaterialUri}.")]
    private partial void LogAssetStatusFailed(Exception exception, Uri materialUri);
}
