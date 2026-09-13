// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Shared concise status wording for browser rows and typed choices.</summary>
public static class AssetStatusPresentation
{
    /// <summary>Returns the current user-facing state without claiming runtime readiness from file presence.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The selected applicable cook, if any.</param>
    /// <param name="hasUnsavedChanges">An immediate edit in the consuming document, before its shared notification arrives.</param>
    /// <returns>A short status, or null when this identity has no authored cook status.</returns>
    public static string? GetText(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false)
        => activity?.State is CookRunState.NeedsSave ? "Needs save"
            : hasUnsavedChanges || status?.HasUnsavedChanges == true ? "Unsaved changes"
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
                AssetCookFreshness.Unknown => "Status unavailable",
                _ when status.HasPublishedOutput && !status.HasVerifiedOutput => "Cooked content invalid",
                AssetCookFreshness.NeedsCooking => "Needs cooking",
                AssetCookFreshness.OutOfDate => "Out of date",
                AssetCookFreshness.Current => "Cooked",
                _ => null,
            },
        };

    /// <summary>Gets the semantic WinUI visual state for the same displayed status.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The applicable cook.</param>
    /// <param name="hasUnsavedChanges">Whether the consuming document has newer edits.</param>
    /// <returns>The neutral, caution, critical or success visual state.</returns>
    public static string GetTone(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false)
        => GetText(status, activity, hasUnsavedChanges) switch
        {
            "Unsaved changes" or "Needs save" or "Out of date" => "Caution",
            "Cook failed" or "Source missing" or "Invalid source" or "Cooked content invalid" => "Critical",
            "Cooked" => "Success",
            _ => "Neutral",
        };

    /// <summary>Explains the next action and existing cooked output without expanding the compact status.</summary>
    /// <param name="status">Saved-source and publication facts.</param>
    /// <param name="activity">The applicable cook.</param>
    /// <param name="hasUnsavedChanges">Whether the consuming document has newer edits.</param>
    /// <returns>A tooltip suitable for a compact asset status.</returns>
    public static string GetDescription(AssetCookStatus? status, AssetCookActivity? activity, bool hasUnsavedChanges = false)
    {
        var text = GetText(status, activity, hasUnsavedChanges);
        var description = text switch
        {
            "Needs save" => "Cooking is waiting for unsaved documents. Open Cooking to review them.",
            "Unsaved changes" => "Save changes before cooking. Automatic cooking follows Save unless paused.",
            "Queued" => "Waiting to cook the latest saved inputs.",
            "Cooking" => "Cooking the saved inputs.",
            "Updating preview" => "Installing cooked content and updating the preview.",
            "Cancelling" => "Stopping at a safe point. Published content will be preserved.",
            "Cook failed" => "Cooking failed. Open Cooking for details and Retry.",
            "Source missing" => "Restore the missing source or choose a replacement.",
            "Invalid source" => "Correct the source errors before cooking.",
            "Cooked content invalid" => "Cook again to replace missing or damaged output.",
            "Needs cooking" => "No cooked content yet. Cooking is needed to use this asset in the viewport.",
            "Out of date" => "Saved inputs have changed. Cooking is needed to update the output.",
            "Cooked" => "Cooked content matches the saved inputs. No cooking is needed.",
            _ => "Cooking status is unavailable.",
        };
        return text is not ("Cooked" or "Updating preview") && status?.HasVerifiedOutput == true
            ? description + " Previously cooked content remains available." : description;
    }
}
