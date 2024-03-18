// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators;

using Microsoft.Extensions.DependencyInjection;

/// <summary>Attribute for marking a class or interface for automatic dependency injection registration.</summary>
/// <param name="lifetime">The <see cref="ServiceLifetime" /> for the annotated type.</param>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Interface, Inherited = false)]
public sealed class InjectAsAttribute(ServiceLifetime lifetime) : Attribute
{
    /// <summary>Gets the specified <see cref="Lifetime" /> of the service.</summary>
    public ServiceLifetime Lifetime { get; } = lifetime;

    /// <summary>Gets or sets the key for the service, if any.</summary>
    public string? Key { get; set; }

    /// <summary>Gets or sets the implementation type, if the target is an interface.</summary>
    public Type? ImplementationType { get; set; }

    /// <summary>Gets or sets the type for which the service will be registered. If not provided, the target type will be used.</summary>
    public Type? ContractType { get; set; }
}
