// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections;

/// <summary>
/// Represents a collection of <see cref="Parameter">URL parameters</see>.
/// </summary>
public class Parameters : IParameters
{
    // Parameter names are case-insensitive.
    private readonly Dictionary<string, string?> parameters = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Gets the number of parameters in the collection.</summary>
    public int Count => this.parameters.Count;

    public bool IsEmpty => this.parameters.Count == 0;

    /// <inheritdoc />
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc />
    public bool Contains(string parameterName) => this.parameters.ContainsKey(parameterName);

    /// <summary>
    /// Adds or updates a parameter with the specified name and value.
    /// </summary>
    /// <param name="name">The name of the parameter to add.</param>
    /// <param name="value">The value of the parameter to add.</param>
    /// <exception cref="ArgumentException">If the specified parameter name is
    /// the empty string.</exception>
    /// <remarks>
    /// As long as the parameter name is valid, this method is always
    /// successful. If a parameter with the same name does not exist in the
    /// collection, a new one with the specified name and value is created and
    /// added. Otherwise, the existing parameter value is updated with the
    /// specified value.
    /// </remarks>
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
    /// <param name="name">The name of the parameter to add.</param>
    /// <param name="value">The value of the parameter to add.</param>
    /// <exception cref="ArgumentException">If the specified parameter name is
    /// the empty string, or if a parameter with the same name exists in the
    /// collection.</exception>
    public void Add(string name, string? value)
    {
        if (name.Length == 0)
        {
            throw new ArgumentException("Segment parameters cannot have an empty name.", nameof(name));
        }

        this.parameters.Add(name, value);
    }

    /// <summary>
    /// Attempts the add a parameter with the specified name and value to the
    /// collection.
    /// </summary>
    /// <param name="name">The name of the parameter to add.</param>
    /// <param name="value">The value of the parameter to add.</param>
    /// <returns><see langword="true" /> if the parameter was added
    /// successfully; <see langword="false" /> otherwise.</returns>
    /// <exception cref="ArgumentException">If the specified parameter name is
    /// the empty string.</exception>
    /// <remarks>
    /// This method doesn't throw an exception if a parameter with the same name
    /// exists in the collection. Unlike the <see cref="AddOrUpdate" />, TryAdd
    /// doesn't override an existing parameter. If a parameter with the same
    /// name exists, TryAdd does nothing and returns <see langword="false" />.
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
    /// <returns><see langword="true" /> if the parameter is successfully found
    /// and removed; otherwise, <see langword="false" />. This method returns
    /// <see langword="false" /> if parameter name is not found in the
    /// collection.</returns>
    public bool Remove(string name) => this.parameters.Remove(name);

    /// <summary>
    /// Removes all items from the collection and resets the <see cref="Count" /> property to <c>0</c>.
    /// </summary>
    public void Clear() => this.parameters.Clear();

    public ReadOnlyParameters AsReadOnly() => new(this);

    /// <inheritdoc />
    public IEnumerator<Parameter> GetEnumerator()
    {
        foreach (var item in this.parameters)
        {
            yield return new Parameter(item.Key, item.Value);
        }
    }

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
