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
/// <param name="SourceFile">The material's source file, in a source mount inside or outside the project tree.</param>
/// <param name="FailFast">Whether pipeline failures should abort immediately.</param>
public sealed record MaterialCookRequest(
    AssetPath Material,
    FileSystemPath ProjectRoot,
    FileSystemPath SourceFile,
    bool FailFast = false);
