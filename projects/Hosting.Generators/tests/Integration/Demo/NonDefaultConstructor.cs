// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators.Demo;

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.DependencyInjection;

[ExcludeFromCodeCoverage]
[InjectAs(ServiceLifetime.Transient)]
internal sealed class NonDefaultConstructor
{
    public NonDefaultConstructor()
    {
    }

    // This constructor should be chosen when this type is resolved by the DI resolver.
    public NonDefaultConstructor([FromKeyedServices("mine")] IMyInterface injected) => this.Injected = injected;

    public IMyInterface? Injected { get; }
}
