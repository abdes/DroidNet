// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Resources.Tests.Fakes;

[ExcludeFromCodeCoverage]
internal sealed class ThrowingResourceMap : IResourceMap
{
    public bool TryGetValue(string key, out string? value) => throw new InvalidOperationException("Simulated failure");

    public IResourceMap GetSubtree(string key) => throw new InvalidOperationException("Simulated failure");
}
