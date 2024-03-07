// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators.Demo;

using Microsoft.Extensions.DependencyInjection;

[InjectAs(ServiceLifetime.Singleton, Key = "mine", ImplementationType = typeof(MyInterfaceImplementation))]
public interface IMyInterface;
