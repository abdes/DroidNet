// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

namespace DroidNet.Resources.Tests.Fakes;

/// <summary>
///     Fake implementation of <see cref="IResourceMapProvider"/> for testing.
/// </summary>
internal sealed class FakeResourceMapProvider : IResourceMapProvider
{
    private readonly IResourceMap applicationResourceMap;
    private readonly IResourceMap assemblyResourceMap;

    public FakeResourceMapProvider(IResourceMap applicationResourceMap, IResourceMap assemblyResourceMap)
    {
        this.applicationResourceMap = applicationResourceMap;
        this.assemblyResourceMap = assemblyResourceMap;
    }

    public IResourceMap ApplicationResourceMap => this.applicationResourceMap;

    public IResourceMap GetAssemblyResourceMap(Assembly assembly) => this.assemblyResourceMap;
}
