// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Read-only scene metadata exposed by the active project context.
/// </summary>
/// <param name="Id">The scene identity.</param>
/// <param name="Name">The scene display name.</param>
public sealed record ProjectSceneInfo(Guid Id, string Name);
