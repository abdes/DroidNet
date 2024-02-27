// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a collection of <see cref="Parameter">URL parameters</see> that
/// also acts like a dictionary, where the keys are the parameter names and the
/// values are the parameter values.
/// </summary>
/// <remarks>
/// For the purpose of indexing the collection of parameters, the parameter
/// names are case-insensitive.
/// <para>
/// Although, semantically, this is a dictionary of
/// parameters names/values, using a dedicated interface type will allow for
/// augmenting the semantics with domain specific behaviors, without impacting
/// the generic collection types.
/// </para>
/// </remarks>
public interface IParameters : IEnumerable<Parameter>
{
    public int Count { get; }

    public bool IsEmpty { get; }

    /// <summary>
    /// Gets the value associated with the specified parameter name.
    /// </summary>
    /// <param name="name">The parameter name whose value to get.</param>
    /// <param name="value">When this method returns, the value associated
    /// with the specified parameter name, if such parameter exists; otherwise,
    /// <see langword="null" />. This parameter is passed uninitialized.</param>
    /// <returns><see langword="true" /> a parameter with the specified name
    /// exists; otherwise, <see langword="false" />.</returns>
    public bool TryGetValue(string name, out string? value);

    /// <summary>
    /// Determines whether the collection contains a parameter with the
    /// specified name.
    /// </summary>
    /// <param name="parameterName">The parameter name to locate.</param>
    /// <returns><see langword="true" /> is the collection contains a parameter
    /// with the specified name; otherwise <see langword="false" />.</returns>
    /// <remarks>
    /// This method compares parameter names in a case-insensitive fashion.
    /// </remarks>
    public bool Contains(string parameterName);
}

/// <summary>
/// Represents a URL segment matrix parameter or a URL query parameter, which
/// have a name and an optional value. The name and the value (when present) are
/// completely opaque strings and do not carry any semantic value inside this
/// implementation.
/// </summary>
public readonly record struct Parameter(string Name, string? Value)
{
    /// <summary>Produces a string representation of this parameter.</summary>
    /// <returns>The parameter name followed by its value, separated by the
    /// equal (<see langword="=" />) sign, unless the parameter has no value. In
    /// such case, only the parameter name.</returns>
    public override string ToString() => Uri.EscapeDataString(this.Name) +
                                         (this.Value is not null
                                             ? '=' + Uri.EscapeDataString(this.Value)
                                             : string.Empty);
}
