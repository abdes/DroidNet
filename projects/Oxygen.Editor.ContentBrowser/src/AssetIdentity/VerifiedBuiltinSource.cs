// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Associates a verified project contribution with its engine-provided identity.</summary>
/// <param name="Root">The physical published root covered by provenance.</param>
/// <param name="CookedUri">The native contribution identity.</param>
/// <param name="Origin">The engine catalog row that produced this contribution.</param>
public sealed record VerifiedBuiltinSource(string Root, Uri CookedUri, ContentBrowserAssetItem Origin);
