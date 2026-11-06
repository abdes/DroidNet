// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets;

/// <summary>
/// Turns a file reference, such as a texture's <c>source</c>, into a file. The asset readers need only this of the
/// project that holds them.
/// </summary>
public interface IFileReferenceLocator
{
    /// <summary>Finds the file a reference names.</summary>
    /// <param name="referrer">The identity of the file that holds the reference.</param>
    /// <param name="text">The reference as written.</param>
    /// <returns>The file, which need not exist; or <see langword="null"/> when the reference does not resolve, or names a
    /// mount that is not bound.</returns>
    FileSystemPath? LocateReference(AssetPath referrer, string text);
}
