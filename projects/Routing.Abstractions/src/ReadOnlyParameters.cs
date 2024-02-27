// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Collections;

/// <summary>
/// Represents a read-only collection of <see cref="Parameter">URL
/// parameters</see>.
/// </summary>
public class ReadOnlyParameters : IParameters
{
    private readonly Parameters parameters;

    /// <summary>
    /// Initializes a new instance of the <see cref="ReadOnlyParameters" />
    /// class that is a wrapper around the specified <see cref="Parameters" />
    /// object.
    /// </summary>
    /// <remarks>
    /// This constructor is internal only because we want ReadOnlyParameters to
    /// be created only through the <see cref="Parameters.AsReadOnly" /> method.
    /// <para>
    /// We do not use the interface type here. We use the concrete class because
    /// the interface would allow a ReadOnlyParameters object to wrap anything
    /// that implements the interface, thus allowing, for example, to wrap
    /// another ReadOnlyParameters, which is not very helpful and should be
    /// avoided.
    /// </para>
    /// </remarks>
    /// <param name="parameters">The parameters to wrap.</param>
    internal ReadOnlyParameters(Parameters parameters) => this.parameters = parameters;

    /// <summary>Gets an empty <see cref="ReadOnlyParameters" />.</summary>
    /// <remarks>
    /// The returned instance is immutable and will always be empty.
    /// </remarks>
    public static ReadOnlyParameters Empty { get; } = new([]);

    /// <summary>Gets the number of parameters in the collection.</summary>
    public int Count => this.parameters.Count;

    public bool IsEmpty => this.parameters.IsEmpty;

    /// <inheritdoc />
    public bool Contains(string parameterName) => this.parameters.Contains(parameterName);

    /// <inheritdoc />
    public bool TryGetValue(string name, out string? value) => this.parameters.TryGetValue(name, out value);

    /// <inheritdoc />
    public IEnumerator<Parameter> GetEnumerator()
    {
        foreach (var param in this.parameters)
        {
            yield return param;
        }
    }

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();
}
