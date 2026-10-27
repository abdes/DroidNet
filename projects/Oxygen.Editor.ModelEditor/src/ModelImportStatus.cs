// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor;

/// <summary>Reads where a model's import stands from the project's assets, and says it.</summary>
internal static class ModelImportStatus
{
    /// <summary>Gets where a model's import stands: the cook state of its scene product, which follows the source.</summary>
    /// <param name="source">The model's source file.</param>
    /// <param name="assets">The project's assets.</param>
    /// <returns>The model's import state.</returns>
    public static ModelImportState Of(AssetPath source, IReadOnlyList<AssetEntry> assets)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(assets);
        if (AssetFileTypes.SceneProductOf(source) is not { } product
            || assets.FirstOrDefault(asset => asset.Identity == product) is not { } entry)
        {
            return ModelImportState.NotImported;
        }

        return entry.CookActivity?.State switch
        {
            CookRunState.Queued or CookRunState.Preparing or CookRunState.Cooking or CookRunState.Validating
                or CookRunState.Publishing or CookRunState.Cancelling => ModelImportState.Importing,
            CookRunState.Failed => ModelImportState.ImportFailed,
            _ => entry.CookStatus?.Freshness switch
            {
                AssetCookFreshness.OutOfDate => ModelImportState.SourceChanged,
                AssetCookFreshness.MissingSource => ModelImportState.SourceMissing,
                AssetCookFreshness.InvalidSource => ModelImportState.ImportFailed,
                AssetCookFreshness.NeedsCooking => ModelImportState.NotImported,
                _ => ModelImportState.Imported,
            },
        };
    }

    /// <summary>Gets what the toolbar says of an import state.</summary>
    /// <param name="state">The state.</param>
    /// <returns>The status text, such as "Source changed".</returns>
    public static string TextOf(ModelImportState state) => state switch
    {
        ModelImportState.NotImported => "Not imported",
        ModelImportState.Imported => "Imported",
        ModelImportState.SourceChanged => "Source changed",
        ModelImportState.Importing => "Importing…",
        ModelImportState.ImportFailed => "Import failed",
        ModelImportState.SourceMissing => "Source missing",
        _ => throw new ArgumentOutOfRangeException(nameof(state), state, "Unknown import state."),
    };

    /// <summary>Gets a value indicating whether a model is worth reimporting: its source changed, or its last import failed.</summary>
    /// <param name="state">The model's import state.</param>
    /// <returns><see langword="true"/> when <em>Reimport</em> is enabled.</returns>
    public static bool CanReimport(ModelImportState state) => state is ModelImportState.SourceChanged or ModelImportState.ImportFailed;

    /// <summary>Gets how serious an import state is, which colors the toolbar's status.</summary>
    /// <param name="state">The model's import state.</param>
    /// <returns>
    /// <see cref="DiagnosticSeverity.Error"/> for an import that failed or a source that is missing,
    /// <see cref="DiagnosticSeverity.Warning"/> for an import the source no longer matches or that never ran, and
    /// <see cref="DiagnosticSeverity.Info"/> otherwise.
    /// </returns>
    public static DiagnosticSeverity SeverityOf(ModelImportState state) => state switch
    {
        ModelImportState.ImportFailed or ModelImportState.SourceMissing => DiagnosticSeverity.Error,
        ModelImportState.SourceChanged or ModelImportState.NotImported => DiagnosticSeverity.Warning,
        _ => DiagnosticSeverity.Info,
    };
}
