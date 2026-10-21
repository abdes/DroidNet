// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.Assets;

/// <summary>The status wording shared by every surface that shows an asset, so the same state reads the same everywhere.</summary>
public static class AssetStatusText
{
    /// <summary>Returns the status an asset list shows beside an asset's name.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The packaged badge, the cook status, or the asset's state badge when it has no cook status.</returns>
    public static string ForEntry(AssetEntry asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var state = asset.PrimaryState is AssetState.Broken or AssetState.Missing ? asset.PrimaryState : asset.DerivedState ?? asset.PrimaryState;
        return asset.IsPackaged ? asset.PackagedBadge
            : GetText(asset.CookStatus, asset.CookActivity, runtimeAvailability: asset.RuntimeAvailability) ?? AssetEntry.GetBadge(state);
    }

    /// <summary>Returns the current user-facing state without claiming runtime readiness from file presence.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The selected applicable cook, if any.</param>
    /// <param name="hasUnsavedChanges">An immediate edit in the consuming document, before its shared notification arrives.</param>
    /// <param name="runtimeAvailability">Acknowledged native availability for this identity.</param>
    /// <returns>A short status, or null when this identity has no authored cook status.</returns>
    public static string? GetText(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false, AssetRuntimeAvailability runtimeAvailability = AssetRuntimeAvailability.Unknown)
        => hasUnsavedChanges || status?.HasUnsavedChanges == true ? "Unsaved changes"
            : status is null ? null : activity?.State switch
            {
                CookRunState.Queued => "Queued",
                CookRunState.Preparing or CookRunState.Cooking or CookRunState.Validating => "Cooking",
                CookRunState.Publishing => "Updating preview",
                CookRunState.Cancelling => "Cancelling",
                CookRunState.Failed => "Cook failed",
                _ => status.Freshness switch
                {
                    AssetCookFreshness.MissingSource => "Source missing",
                    AssetCookFreshness.InvalidSource => "Invalid source",
                    AssetCookFreshness.Unknown when status.Diagnostics.Any(static diagnostic => diagnostic.Severity == Oxygen.Managed.Core.Diagnostics.DiagnosticSeverity.Error) => "Status unavailable",
                    AssetCookFreshness.Unknown => "Status pending",
                    _ when status.HasPublishedOutput && status.OutputAvailability == CookedOutputAvailability.Missing => "Needs cooking",
                    _ when status.HasPublishedOutput && status.OutputAvailability == CookedOutputAvailability.Unknown => "Status pending",
                    AssetCookFreshness.NeedsCooking => "Needs cooking",
                    AssetCookFreshness.OutOfDate => "Out of date",
                    AssetCookFreshness.Current when runtimeAvailability == AssetRuntimeAvailability.Failed => "Preview issue",
                    AssetCookFreshness.Current when runtimeAvailability == AssetRuntimeAvailability.Updating => "Updating preview",
                    AssetCookFreshness.Current when status.HasAvailableOutput && runtimeAvailability == AssetRuntimeAvailability.Mounted => "Ready",
                    AssetCookFreshness.Current => "Cooked",
                    _ => null,
                },
            };

    /// <summary>Gets the semantic WinUI visual state for the same displayed status.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The applicable cook.</param>
    /// <param name="hasUnsavedChanges">Whether the consuming document has newer edits.</param>
    /// <param name="runtimeAvailability">Acknowledged native availability for this identity.</param>
    /// <returns>The neutral, caution, critical or success visual state.</returns>
    public static string GetTone(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false, AssetRuntimeAvailability runtimeAvailability = AssetRuntimeAvailability.Unknown)
        => GetToneForText(GetText(status, activity, hasUnsavedChanges, runtimeAvailability));

    /// <summary>Gets the semantic visual state of a displayed status.</summary>
    /// <param name="text">The displayed status.</param>
    /// <returns>The neutral, caution, critical or success visual state.</returns>
    public static string GetToneForText(string? text)
        => text switch
        {
            "Unsaved changes" or "Out of date" or "Status unavailable" or "Overridden" or "Preview unavailable" or "STALE" => "Caution",
            "Cook failed" or "Source missing" or "Invalid source" or "Preview issue" or "ERR" or "MISS" => "Critical",
            "Cooked" or "Ready" => "Success",
            _ => "Neutral",
        };

    /// <summary>Explains the next action and existing cooked output without expanding the compact status.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The applicable cook.</param>
    /// <param name="hasUnsavedChanges">Whether the consuming document has newer edits.</param>
    /// <param name="runtimeAvailability">Acknowledged native availability for this identity.</param>
    /// <param name="runtimeReason">The runtime's current availability explanation.</param>
    /// <returns>A tooltip suitable for a compact asset status.</returns>
    public static string GetDescription(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false, AssetRuntimeAvailability runtimeAvailability = AssetRuntimeAvailability.Unknown, string? runtimeReason = null)
    {
        var text = GetText(status, activity, hasUnsavedChanges, runtimeAvailability);
        var description = text switch
        {
            "Unsaved changes" => "An open document has unsaved changes. Cooking reads the saved file; Cook asks to save it first.",
            "Queued" => "Waiting to cook the latest saved inputs.",
            "Cooking" => "Cooking the saved inputs.",
            "Updating preview" => "Installing cooked content and updating the preview.",
            "Cancelling" => "Stopping at a safe point. Published content will be preserved.",
            "Cook failed" => "Cooking failed. Open Cooking for details and Retry.",
            "Source missing" => "Restore the missing source or choose a replacement.",
            "Invalid source" => "Correct the source errors before cooking.",
            "Status unavailable" => string.Join(' ', status!.Diagnostics.Select(static diagnostic => diagnostic.Message)),
            "Status pending" => "Waiting for current source, publication or engine information.",
            "Needs cooking" when status?.HasPublishedOutput == true => "Cooked output is missing. Cook again to restore it.",
            "Needs cooking" => "No cooked content yet. Cooking is needed to use this asset in the viewport.",
            "Out of date" => "Saved inputs have changed. Cooking is needed to update the output.",
            "Cooked" => "Cooked content matches the saved inputs. No cooking is needed.",
            "Ready" => "Cooked content matches the saved inputs and is available in the preview.",
            "Preview issue" => "Cooked content is current, but a native load or application failed.",
            _ => "Cooking status is unavailable.",
        };
        if (text is not ("Cooked" or "Ready" or "Updating preview") && status?.HasAvailableOutput == true)
        {
            description += " Previously cooked content remains available.";
        }

        return string.IsNullOrEmpty(runtimeReason) ? description : description + " Preview: " + runtimeReason;
    }
}
