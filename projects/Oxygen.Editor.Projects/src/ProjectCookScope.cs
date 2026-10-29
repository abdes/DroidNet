// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     The project a cook runs for. Every output path is derived from <see cref="ProjectRoot"/> through
///     <see cref="ProjectOutputFolders"/>.
/// </summary>
/// <param name="ProjectId">The project identity.</param>
/// <param name="ProjectRoot">The normalized project root path.</param>
public sealed record ProjectCookScope(Guid ProjectId, FileSystemPath ProjectRoot);
