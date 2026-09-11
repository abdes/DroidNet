// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Identifies the logical scope of a cook independently of its captured inputs.</summary>
/// <param name="TargetKind">Whether the request cooks an asset, scene, folder, or project.</param>
/// <param name="ScopeUri">The source asset/folder identity; null for the entire project.</param>
/// <param name="IsAutomatic">Whether the request must remain quiet in the workspace.</param>
public sealed record CookRunRequest(CookTargetKind TargetKind, Uri? ScopeUri, bool IsAutomatic = false);
