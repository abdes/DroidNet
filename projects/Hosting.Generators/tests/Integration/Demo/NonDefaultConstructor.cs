// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators.Demo;

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.DependencyInjection;

[ExcludeFromCodeCoverage]
[InjectAs(ServiceLifetime.Transient)]
[method: ActivatorUtilitiesConstructor]
internal sealed class NonDefaultConstructor([FromKeyedServices("mine")] IMyInterface injected)
{
    public IMyInterface Injected { get; } = injected;
}
