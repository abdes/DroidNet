// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Minimal project cook scope available in ED-M01.
/// </summary>
/// <param name="ProjectId">The project identity.</param>
/// <param name="ProjectRoot">The normalized project root path.</param>
/// <param name="CookedOutputRoot">The normalized cooked output root path.</param>
public sealed record ProjectCookScope(Guid ProjectId, string ProjectRoot, string CookedOutputRoot);
