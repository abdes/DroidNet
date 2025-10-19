// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Sources;

/// <summary>
///     No-op encryption provider that returns the input bytes unchanged.
/// </summary>
public class NoEncryptionProvider : IEncryptionProvider
{
    /// <summary>
    /// Gets a singleton instance of the no-op encryption provider.
    /// </summary>
    public static NoEncryptionProvider Instance { get; } = new NoEncryptionProvider();

    /// <inheritdoc/>
    public byte[] Decrypt(byte[] data) => data;

    /// <inheritdoc/>
    public byte[] Encrypt(byte[] data) => data;
}
