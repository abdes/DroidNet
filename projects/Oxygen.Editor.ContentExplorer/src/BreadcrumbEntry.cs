// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer;

/// <summary>
/// Represents one breadcrumb segment: its label and the folder it opens.
/// </summary>
/// <param name="Label">The segment's label.</param>
/// <param name="Folder">The folder; <see langword="null"/> for the project, which browses every folder.</param>
public sealed record BreadcrumbEntry(string Label, AssetPath? Folder);
