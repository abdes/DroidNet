// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>Locates file references in a fixed table of bound mounts, as a project does.</summary>
/// <param name="mounts">The folder of each bound mount, by the mount's name.</param>
public sealed class MountFileLocator(IReadOnlyDictionary<string, FileSystemPath> mounts) : IFileReferenceLocator
{
    /// <inheritdoc/>
    public FileSystemPath? LocateReference(AssetPath referrer, string text)
        => AssetPath.TryResolveReference(referrer, text, VirtualPathRuleSet.Cooked, out var path, out _)
            && mounts.TryGetValue(path.Mount, out var folder)
                ? folder.Combine(path.RelativePath)
                : null;
}
