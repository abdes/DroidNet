// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Filesystem location of a material source descriptor within an active project.
/// </summary>
/// <param name="MaterialUri">The material source asset URI.</param>
/// <param name="ProjectRoot">The absolute project root.</param>
/// <param name="MountName">The authoring mount name used in the material asset URI.</param>
/// <param name="SourcePath">The absolute material descriptor path.</param>
/// <param name="SourceRelativePath">The project-relative material descriptor path.</param>
public sealed record MaterialSourceLocation(
    Uri MaterialUri,
    string ProjectRoot,
    string MountName,
    string SourcePath,
    string SourceRelativePath);
