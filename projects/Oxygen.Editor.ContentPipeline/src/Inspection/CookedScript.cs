// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scripting;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A cooked script as the native Inspector reports it: its native asset key and its declarations.</summary>
/// <remarks>
/// A cooked script keeps each parameter's name, type, default value and asset type; authoring hints such as range,
/// unit, description, options and color stay in the source and are not cooked.
/// </remarks>
/// <param name="AssetKey">The native asset key.</param>
/// <param name="Script">The script, with <see cref="ScriptOrigin.Library"/>.</param>
public sealed record CookedScript(Guid AssetKey, Script Script);
