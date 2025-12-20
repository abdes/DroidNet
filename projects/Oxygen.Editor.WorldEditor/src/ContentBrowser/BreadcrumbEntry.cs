// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.ContentBrowser;

/// <summary>
/// Represents one breadcrumb segment with a label and the accumulated relative path.
/// </summary>
public sealed record BreadcrumbEntry(string Label, string RelativePath);
