// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Demo.Welcome;

using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

[InjectAs(ServiceLifetime.Transient)]
public class WelcomeViewModel(IDock dock)
{
}
