// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>What a content mount exposes.</summary>
public enum ContentMountKind
{
    /// <summary>A source folder: authored content, the source of asset identity.</summary>
    ProjectSource,

    /// <summary>The Cooked or Build output folder node; view state, never a mount, and read-only.</summary>
    Derived,

    /// <summary>A cooked library, bound to a loose root, a <c>.cooked</c> folder or a pak; read-only, and named by its content.</summary>
    Library,

    /// <summary>The SDK's engine content pak, beneath every other mount; always mounted and read-only.</summary>
    EngineContent,
}
