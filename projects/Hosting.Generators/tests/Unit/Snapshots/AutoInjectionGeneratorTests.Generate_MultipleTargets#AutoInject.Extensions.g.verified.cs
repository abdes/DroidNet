//HintName: AutoInject.Extensions.g.cs
// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using Microsoft.Extensions.DependencyInjection;

public static class AutoInjectExtensions
{
    public static IServiceCollection UseAutoInject(this IServiceCollection services)
    {
        _ = services.AddKeyedTransient<Testing.ITestInterface>("key", (sp, _) => ActivatorUtilities.CreateInstance<Testing.TestClass>(sp));
        _ = services.AddTransient(sp => ActivatorUtilities.CreateInstance<Testing.TestClass>(sp));

        return services;
    }
}
