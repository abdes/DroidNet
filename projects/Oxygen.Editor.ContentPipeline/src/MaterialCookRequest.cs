// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Request to cook a single material source descriptor.
/// </summary>
/// <param name="Material">The material's identity.</param>
/// <param name="ProjectRoot">The absolute project root.</param>
/// <param name="MountName">The authoring mount that owns the material source.</param>
/// <param name="SourceRelativePath">The project-relative material source path.</param>
/// <param name="FailFast">Whether pipeline failures should abort immediately.</param>
public sealed record MaterialCookRequest(
    AssetPath Material,
    FileSystemPath ProjectRoot,
    string MountName,
    string SourceRelativePath,
    bool FailFast = false);
