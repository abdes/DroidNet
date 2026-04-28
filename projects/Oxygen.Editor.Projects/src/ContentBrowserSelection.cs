// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Content Browser folder selection passed to project-layout target resolution.
/// </summary>
/// <param name="ProjectRelativeFolder">Project-relative folder selected in the Content Browser.</param>
/// <param name="LocalMountName">Explicit local mount name selected as a creation target, if any.</param>
public sealed record ContentBrowserSelection(string? ProjectRelativeFolder, string? LocalMountName = null);
