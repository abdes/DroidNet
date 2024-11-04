// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections;

/// <summary>
/// Represents a collection of URL parameters (<see cref="Parameter" />).
/// </summary>
public class Parameters : IParameters
{
    /// <summary>Dictionary to store parameters, with case-insensitive names.</summary>
    private readonly Dictionary<string, string?> parameters = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Gets the number of parameters in the collection.</summary>
    public int Count => this.parameters.Count;

    /// <summary>Gets a value indicating whether the collection is empty.</summary>
    public bool IsEmpty => this.parameters.Count == 0;

    /// <inheritdoc />
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc />
    public bool Contains(string parameterName) => this.parameters.ContainsKey(parameterName);

    /// <summary>
    /// Adds or updates a parameter with the specified name and value.
    /// If the parameter exists, its value is updated; otherwise, a new parameter is added.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The value of the parameter.</param>
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
    /// <param name="value">The value of the parameter.</param>
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
    /// Attempts to add a parameter with the specified name and value to the collection.
    /// </summary>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The value of the parameter.</param>
    /// <returns><see langword="true" /> if the parameter was added successfully; otherwise, <see langword="false" />.</returns>
    /// <exception cref="ArgumentException">Thrown if the parameter name is empty.</exception>
    /// <remarks>
    /// This method does not throw an exception if a parameter with the same name exists in the collection. Instead,
    /// it returns <see langword="false" />.
    /// </remarks>
    public bool TryAdd(string name, string? value)
    {
        if (name.Length == 0)
        {
            throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(name));
        }

        return this.parameters.TryAdd(name, value);
    }

    /// <summary>
    /// Removes the parameter with the specified name from the collection.
    /// </summary>
    /// <param name="name">The name of the parameter to remove.</param>
    /// <returns>
    /// <see langword="true" /> if the parameter is successfully removed; otherwise, <see langword="false" />.
    /// </returns>
    public bool Remove(string name) => this.parameters.Remove(name);

    /// <summary>
    /// Clears all parameters from the collection, resetting the <see cref="Count" /> property to zero.
    /// </summary>
    public void Clear() => this.parameters.Clear();

    /// <summary>
    /// Returns a read-only version of the parameters collection.
    /// </summary>
    /// <returns>A <see cref="ReadOnlyParameters">read-only</see> version of the parameters collection.</returns>
    public ReadOnlyParameters AsReadOnly() => new(this);

    /// <inheritdoc />
    public IEnumerator<Parameter> GetEnumerator()
        => this.parameters.Select(item => new Parameter(item.Key, item.Value)).GetEnumerator();

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
