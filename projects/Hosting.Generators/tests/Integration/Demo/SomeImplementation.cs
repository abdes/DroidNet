// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators.Demo;

using System.Diagnostics.CodeAnalysis;
using Microsoft.Extensions.DependencyInjection;

// Does not implement ITargetInterface
[ExcludeFromCodeCoverage]
[InjectAs(ServiceLifetime.Scoped)]
internal sealed class SomeImplementation;
