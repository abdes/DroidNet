// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// What a project mount holds, as the engine's mount contract names it. A mount's kind is committed with its
/// declaration; a <c>.mounts.json</c> binds or overrides its path, never its kind.
/// </summary>
[JsonConverter(typeof(MountKindJsonConverter))]
public enum MountKind
{
    /// <summary>A folder of authored sources: edited, cooked, relocated and watched.</summary>
    Source = 0,

    /// <summary>
    /// A read-only cooked library; its binding decides how it is read: a loose cooked root, a <c>.pak</c> file, or a
    /// library project's <c>.cooked</c> folder.
    /// </summary>
    Cooked = 1,
}
