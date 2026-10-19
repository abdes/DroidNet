// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>A mount whose folders are derived from its assets' virtual paths rather than read from disk.</summary>
public interface ILogicalFolderRoot
{
    /// <summary>Gets the mount's virtual root, the identity the browser selects it by.</summary>
    public AssetPath VirtualRootPath { get; }
}
