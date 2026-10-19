// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>Classifies loose-index file kinds.</summary>
public static class FileKindExtensions
{
    /// <summary>
    /// Whether records of this kind are identified by their path: a root holds any number of auxiliary files and
    /// texture descriptors, but one file per resource-table role.
    /// </summary>
    /// <param name="kind">The file kind.</param>
    /// <returns><see langword="true"/> for auxiliary files and texture descriptors.</returns>
    public static bool IsPathKeyed(this FileKind kind) => kind is FileKind.Auxiliary or FileKind.TextureDescriptor;
}
