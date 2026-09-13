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
    /// <returns>A short status, or null when this identity has no authored cook status.</returns>
    public static string? GetText(AssetCookStatus? status, AssetCookActivity? activity)
        => status is null ? null
            : activity?.State is CookRunState.NeedsSave ? "Needs save"
            : status.HasUnsavedChanges ? "Unsaved changes" : activity?.State switch
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
}
