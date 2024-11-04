// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections;

/// <summary>
/// Represents a read-only collection of URL parameters (<see cref="Parameter" />).
/// </summary>
public class ReadOnlyParameters : IParameters
{
    /// <summary>
    /// The underlying parameters collection.
    /// </summary>
    private readonly Parameters parameters;

    /// <summary>
    /// Initializes a new instance of the <see cref="ReadOnlyParameters" /> class as a wrapper around the specified
    /// <see cref="Parameters" /> object.
    /// </summary>
    /// <remarks>
    /// This constructor is internal to ensure that <see cref="ReadOnlyParameters" /> objects are created only through
    /// the <see cref="Parameters.AsReadOnly" /> method. The concrete class <see cref="Parameters" /> is used instead
    /// of the interface to prevent wrapping another <see cref="ReadOnlyParameters" /> instance.
    /// </remarks>
    /// <param name="parameters">The parameters collection to wrap.</param>
    internal ReadOnlyParameters(Parameters parameters) => this.parameters = parameters;

    /// <summary>
    /// Gets an empty instance of <see cref="ReadOnlyParameters" />.
    /// </summary>
    /// <remarks>
    /// The returned instance is immutable and will always be empty.
    /// </remarks>
    public static ReadOnlyParameters Empty { get; } = new([]);

    /// <summary>
    /// Gets the number of parameters in the collection.
    /// </summary>
    public int Count => this.parameters.Count;

    /// <summary>
    /// Gets a value indicating whether the collection is empty.
    /// </summary>
    public bool IsEmpty => this.parameters.IsEmpty;

    /// <inheritdoc />
    public bool Contains(string parameterName) => this.parameters.Contains(parameterName);

    /// <inheritdoc />
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc />
    public IEnumerator<Parameter> GetEnumerator() => this.parameters.GetEnumerator();

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
