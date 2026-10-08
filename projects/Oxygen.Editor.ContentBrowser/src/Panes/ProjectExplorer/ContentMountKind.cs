// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>What a content mount exposes.</summary>
public enum ContentMountKind
{
    /// <summary>Authored project content; the source of asset identity.</summary>
    ProjectSource,

    /// <summary>Cooked, Imported or Build output; read-only.</summary>
    Derived,

    /// <summary>A cooked library folder outside the project; read-only.</summary>
    LocalLibrary,
}
