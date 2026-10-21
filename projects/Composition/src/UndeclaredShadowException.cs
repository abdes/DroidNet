// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DryIoc;

namespace DroidNet.Composition;

/// <summary>
///     The exception thrown when a new scope registers a service of its parent chain that none of its
///     modules declares in <see cref="IScopeModule{TLayer}.Shadows" />.
/// </summary>
public sealed class UndeclaredShadowException : InvalidOperationException
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="UndeclaredShadowException" /> class.
    /// </summary>
    public UndeclaredShadowException()
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="UndeclaredShadowException" /> class.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    public UndeclaredShadowException(string message)
        : base(message)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="UndeclaredShadowException" /> class.
    /// </summary>
    /// <param name="message">The message that describes the error.</param>
    /// <param name="innerException">The exception that caused this one.</param>
    public UndeclaredShadowException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="UndeclaredShadowException" /> class.
    /// </summary>
    /// <param name="registrations">The undeclared shadows.</param>
    public UndeclaredShadowException(IReadOnlyList<ServiceRegistrationInfo> registrations)
        : base(Describe(registrations))
    {
        this.Registrations = registrations;
    }

    /// <summary>
    ///     Gets the registrations of the new scope that shadow its parent chain undeclared.
    /// </summary>
    public IReadOnlyList<ServiceRegistrationInfo> Registrations { get; } = [];

    private static string Describe(IReadOnlyList<ServiceRegistrationInfo> registrations)
    {
        ArgumentNullException.ThrowIfNull(registrations);
        return "a scope module shadows a parent service without declaring it: "
            + string.Join(", ", registrations.Select(Describe));
    }

    private static string Describe(ServiceRegistrationInfo registration)
        => registration.OptionalServiceKey is null
            ? registration.ServiceType.ToString()
            : string.Create(CultureInfo.InvariantCulture, $"{registration.ServiceType} (key {registration.OptionalServiceKey})");
}
