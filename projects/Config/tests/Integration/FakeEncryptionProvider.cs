// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Config.Sources;

namespace DroidNet.Config.Tests.Integration;

[ExcludeFromCodeCoverage]
public sealed class FakeEncryptionProvider : IEncryptionProvider
{
    public byte[] Encrypt(byte[] data) => data;

    public byte[] Decrypt(byte[] data) => data;
}
