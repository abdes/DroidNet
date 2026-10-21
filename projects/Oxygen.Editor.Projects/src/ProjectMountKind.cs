// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>The kind of project mount that holds an asset's file.</summary>
public enum ProjectMountKind
{
    /// <summary>An authoring mount: a folder of the project, declared by its manifest.</summary>
    Authoring = 0,

    /// <summary>A local folder mount: a folder outside the project that it lists.</summary>
    LocalFolder = 1,

    /// <summary>
    /// The project's own files: the project's root folder, which holds what no other mount does, such as its
    /// configuration and media.
    /// </summary>
    ProjectFiles = 2,
}
