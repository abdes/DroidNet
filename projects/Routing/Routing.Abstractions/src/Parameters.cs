// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections;
using System.Linq;

namespace DroidNet.Routing;

/// <summary>
/// Represents a collection of URL parameters with case-insensitive names and support for multiple values.
/// </summary>
/// <remarks>
/// <para>
/// This class provides the default implementation of <see cref="IParameters"/>, storing parameter
/// values internally as comma-separated strings. When multiple values are specified for the same
/// parameter, whether through repeated parameters or comma-separated lists, they are normalized
/// into a single comma-separated value.
/// </para>
/// <para>
/// Parameter names are handled case-insensitively, making 'Filter', 'filter', and 'FILTER'
/// equivalent. Values can be <see langword="null"/> (parameter exists without value), empty
/// (parameter with empty value), or contain actual data.
/// </para>
/// </remarks>
public class Parameters : IParameters
{
    /// <summary>Dictionary to store parameters, with case-insensitive names.</summary>
    private readonly Dictionary<string, string?> parameters = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>
    /// Gets the number of distinct parameter names in the collection.
    /// </summary>
    public int Count => this.parameters.Count;

    /// <summary>
    /// Gets a value indicating whether the collection contains any parameters.
    /// </summary>
    public bool IsEmpty => this.parameters.Count == 0;

    /// <summary>
    /// Creates a new parameter collection by combining parent and child parameters.
    /// </summary>
    /// <param name="parentParameters">The parent parameters to merge.</param>
    /// <param name="childParameter">The child parameters to merge.</param>
    /// <returns>
    /// A new <see cref="Parameters"/> instance containing all parameters from both collections,
    /// with child values overriding parent values for duplicate names.
    /// </returns>
    /// <remarks>
    /// Child parameters take precedence over parent parameters when the same parameter name exists
    /// in both collections. This is particularly useful when combining matrix parameters from
    /// different levels of the URL hierarchy.
    /// </remarks>
    public static Parameters Merge(IParameters parentParameters, IParameters childParameter)
    {
        var merged = new Parameters();
        foreach (var pair in parentParameters)
        {
            merged.Add(pair.Name, pair.Value);
        }

        // Merge the child parameters
        foreach (var parameter in childParameter)
        {
            merged.AddOrUpdate(parameter.Name, parameter.Value);
        }

        return merged;
    }

    /// <inheritdoc />
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc />
    public string[]? GetValues(string name)
    {
        if (!this.parameters.TryGetValue(name, out var value))
        {
            // Parameter does not exist
            return null;
        }

        if (value is null)
        {
            // Parameter has no values
            return [];
        }

        return value.Length == 0
            ? [string.Empty] // Has a single value, the empty string
            : value.Split(','); // Has one ore more (comma-separated) values
    }

    /// <inheritdoc />
    public bool Contains(string parameterName) => this.parameters.ContainsKey(parameterName);

    /// <summary>
    /// Adds or updates a parameter with the specified name and value.
    /// If the parameter exists, its value is updated; otherwise, a new parameter is added.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The value(s) of the parameter, encoded as a comma-separated list of values if multiple.</param>
    /// <exception cref="ArgumentException">Thrown if the parameter name is empty.</exception>
    public void AddOrUpdate(string name, string? value)
    {
        if (name.Length == 0)
        {
            throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(name));
        }

        this.parameters[name] = value;
    }

    /// <summary>
    /// Adds a parameter with the specified name and value to the collection.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The value(s) of the parameter, encoded as a comma-separated list of values if multiple.</param>
    /// <exception cref="ArgumentException">
    /// Thrown if the parameter name is empty, or if a parameter with the same name already exists in the collection.
    /// </exception>
    public void Add(string name, string? value)
    {
        if (name.Length == 0)
        {
            throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(name));
        }

        this.parameters.Add(name, value);
    }

    /// <summary>
    /// Attempts to add a new parameter to the collection.
    /// </summary>
    /// <param name="name">The case-insensitive name of the parameter.</param>
    /// <param name="value">The value(s) of the parameter, encoded as a comma-separated list of values if multiple.</param>
    /// <returns>
    /// <see langword="true"/> if the parameter was added successfully; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentException">Thrown if the parameter name is empty.</exception>
    /// <remarks>
    /// This method does not throw an exception if a parameter with the same name exists in the collection. Instead,
    /// it returns <see langword="false"/>.
    /// </remarks>
    public bool TryAdd(string name, string? value) => name.Length == 0
            ? throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(name))
            : this.parameters.TryAdd(name, value);

    /// <summary>
    /// Removes the parameter with the specified name from the collection.
    /// </summary>
    /// <param name="name">The case-insensitive name of the parameter to remove.</param>
    /// <returns>
    /// <see langword="true"/> if the parameter was found and removed; otherwise, <see langword="false"/>.
    /// </returns>
    public bool Remove(string name) => this.parameters.Remove(name);

    /// <summary>
    /// Clears all parameters from the collection.
    /// </summary>
    /// <remarks>
    /// After this operation, <see cref="Count"/> will be 0 and <see cref="IsEmpty"/> will be <see langword="true"/>.
    /// </remarks>
    public void Clear() => this.parameters.Clear();

    /// <summary>
    /// Determines whether this parameter collection is equal to another.
    /// </summary>
    /// <param name="other">The parameter collection to compare with this instance.</param>
    /// <returns>True if both collections contain the same parameters with identical values; otherwise, false.</returns>
    /// <remarks>
    /// Two parameter collections are considered equal if they contain the same number of parameters
    /// and each parameter in this collection has a matching parameter in the other collection with
    /// an identical value. The comparison is case-insensitive for parameter names but case-sensitive
    /// for parameter values.
    /// </remarks>
    public bool Equals(IParameters? other)
    {
        if (other is null)
        {
            return false;
        }

        // If both are empty, they're equal
        if (this.IsEmpty && other.IsEmpty)
        {
            return true;
        }

        // If they have different counts, they're not equal
        if (this.Count != other.Count)
        {
            return false;
        }

        // Compare each parameter by enumerating
        foreach (var param in this)
        {
            if (!other.TryGetValue(param.Name, out var otherValue))
            {
                return false;
            }

            if (!string.Equals(param.Value, otherValue, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    /// <inheritdoc />
    public override bool Equals(object? obj) => obj is IParameters other && this.Equals(other);

    /// <inheritdoc />
    public override int GetHashCode()
    {
        if (this.IsEmpty)
        {
            return 0;
        }

        HashCode hash = default;
        foreach (var pair in this.parameters.OrderBy(p => p.Key, StringComparer.OrdinalIgnoreCase))
        {
            hash.Add(pair.Key, StringComparer.OrdinalIgnoreCase);
            hash.Add(pair.Value ?? string.Empty, StringComparer.Ordinal);
        }

        return hash.ToHashCode();
    }

    /// <summary>
    /// Creates an immutable view of the parameters collection.
    /// </summary>
    /// <returns>
    /// A new <see cref="ReadOnlyParameters"/> instance that provides read-only access to the
    /// current parameters. Any changes to the original collection will not be reflected in the
    /// read-only view.
    /// </returns>
    public ReadOnlyParameters AsReadOnly() => new(this);

    /// <summary>
    /// Implements the generic enumeration of parameters.
    /// </summary>
    /// <returns>
    /// An enumerator that yields <see cref="Parameter"/> instances representing each name-value pair
    /// in the collection.
    /// </returns>
    public IEnumerator<Parameter> GetEnumerator()
        => this.parameters.Select(item => new Parameter(item.Key, item.Value)).GetEnumerator();

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
