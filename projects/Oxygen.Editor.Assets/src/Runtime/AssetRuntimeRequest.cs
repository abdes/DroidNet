// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Runtime;

/// <summary>The outcome of the preview applying one asset.</summary>
/// <param name="Asset">The asset's path as the request named it: a virtual path or an asset URI.</param>
/// <param name="Succeeded">Whether the asset was applied; <see langword="null"/> while it is loading.</param>
/// <param name="Reason">Why the asset could not be applied, when it failed.</param>
public sealed record AssetRuntimeRequest(AssetPath Asset, bool? Succeeded, string? Reason = null);
