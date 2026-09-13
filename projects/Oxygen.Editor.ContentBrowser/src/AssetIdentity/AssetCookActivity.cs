// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>A cook affecting an asset, separate from its existing published output.</summary>
/// <param name="OperationId">The operation shown by the Cooking panel.</param>
/// <param name="State">The current operation state.</param>
public sealed record AssetCookActivity(Guid OperationId, CookRunState State);
