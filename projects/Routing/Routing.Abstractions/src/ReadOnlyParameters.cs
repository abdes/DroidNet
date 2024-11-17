// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;

namespace DroidNet.Routing;

/// <summary>
/// Represents a read-only view of a URL parameters collection.
/// </summary>
/// <remarks>
/// <para>
/// This class provides an immutable wrapper around a parameters collection, ensuring that the
/// contained parameters cannot be modified. It maintains the same case-insensitive parameter
/// naming and value handling as the underlying collection.
/// </para>
/// <para>
/// ReadOnlyParameters instances are typically created through the <see cref="Parameters.AsReadOnly"/>
/// method rather than directly constructed, ensuring proper encapsulation of the underlying collection.
/// </para>
/// </remarks>
public class ReadOnlyParameters : IParameters
{
    /// <summary>
    /// The underlying parameters collection.
    /// </summary>
    private readonly Parameters parameters;

    /// <summary>
    /// Initializes a new instance of the <see cref="ReadOnlyParameters"/> class as a wrapper
    /// around the specified parameters collection.
    /// </summary>
    /// <remarks>
    /// This constructor is internal to ensure that ReadOnlyParameters objects are created only through
    /// the <see cref="Parameters.AsReadOnly"/> method. The concrete <see cref="Parameters"/> class
    /// is used instead of the interface to prevent wrapping another ReadOnlyParameters instance.
    /// </remarks>
    /// <param name="parameters">The parameters collection to wrap.</param>
    internal ReadOnlyParameters(Parameters parameters)
    {
        this.parameters = parameters;
    }

    /// <summary>
    /// Gets an empty instance of <see cref="ReadOnlyParameters"/>.
    /// </summary>
    /// <remarks>
    /// This static property provides a singleton empty collection, avoiding unnecessary instance
    /// creation when no parameters are needed. The returned instance is immutable and will
    /// always be empty.
    /// </remarks>
    public static ReadOnlyParameters Empty { get; } = new([]);

    /// <summary>
    /// Gets the number of distinct parameter names in the collection.
    /// </summary>
    /// <remarks>
    /// Since parameter names are case-insensitive, variations of the same name (e.g., 'filter'
    /// and 'Filter') count as a single parameter.
    /// </remarks>
    public int Count => this.parameters.Count;

    /// <summary>
    /// Gets a value indicating whether the collection contains any parameters.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the collection has no parameters; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsEmpty => this.parameters.IsEmpty;

    /// <inheritdoc/>
    public bool Contains(string parameterName) => this.parameters.Contains(parameterName);

    /// <inheritdoc/>
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc/>
    public string?[]? GetValues(string name) => this.parameters.GetValues(name);

    /// <summary>
    /// Gets an enumerator that iterates through the parameters collection.
    /// </summary>
    /// <returns>
    /// An enumerator that yields <see cref="Parameter"/> instances, each representing a name-value
    /// pair from the collection. Names are yielded in their original case but compare case-insensitively.
    /// </returns>
    public IEnumerator<Parameter> GetEnumerator() => this.parameters.GetEnumerator();

    /// <summary>
    /// Explicitly implements the non-generic IEnumerable interface.
    /// </summary>
    /// <returns>
    /// A non-generic enumerator that iterates through the parameters collection. Uses the generic
    /// implementation internally to maintain consistency.
    /// </returns>
    /// <remarks>
    /// This explicit implementation ensures type safety while maintaining compatibility with non-generic
    /// collection interfaces. It simply forwards to the generic <see cref="GetEnumerator"/> method.
    /// </remarks>
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
