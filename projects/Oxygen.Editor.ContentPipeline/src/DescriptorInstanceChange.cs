// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>One node's changes an instance root writes in a scene descriptor.</summary>
/// <param name="Path">The changed node's path below the instance root.</param>
/// <param name="Names">The node's last known names, or <see langword="null"/> when never known.</param>
public sealed record DescriptorInstanceChange(NodePath Path, string? Names);
