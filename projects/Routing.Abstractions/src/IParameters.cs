// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a collection of URL parameters (<see cref="Parameter" />), functioning like a dictionary where keys are
/// parameter names and values are parameter values.
/// </summary>
/// <remarks>
/// Parameter names are treated as case-insensitive for indexing purposes. The support for multi-valued parameters is
/// flexible, but relies on the following conventions:
/// <para>
/// A parameter may be specified as `<c>name</c>`, indicating that it exists, but has no value (i.e. a <see langword="null" /> value).
/// </para>
/// <para>
/// A parameter may be specified as `<c>name=</c>`, indicating that it exists and has a single value and that value is
/// empty (i.e. <see cref="string.Empty" />).
/// </para>
/// <para>
/// A parameter may be specified as `<c>name=value</c>` or `<c>name=value1,value2</c>`, indicating that it exists and
/// has a "value". That value may be interpreted as a single value, or as comma-separated multiple values. How to store
/// the value(s) in the collection is and implementation detail, but the <see cref="TryGetValue(string,out string?)" />
/// method will always return the value(s) as a single string, while <see cref="GetValues" /> may split a
/// comma-separated value into individual values and always return an array of strings with a single value or multiple
/// values.
/// </para>
/// <para>
/// A parameter may appear multiple times as `<c>name=value1&amp;name=value2</c>`. In such case, its value(s) can be
/// obtained as a single string using <see cref="TryGetValue(string,out string?)" /> or as an array of strings using
/// <see cref="GetValues" />.
/// </para>
/// <para>
/// When encoding parameters and their values, an implementation may interchangeably chose to encode multiple values as
/// a comma-separated list or as multiple occurrences of the same parameter name with a different value each time.
/// </para>
/// </remarks>
public interface IParameters : IEnumerable<Parameter>
{
    /// <summary>
    /// Gets the number of parameters in the collection.
    /// </summary>
    int Count { get; }

    /// <summary>
    /// Gets a value indicating whether the collection is empty.
    /// </summary>
    bool IsEmpty { get; }

    /// <summary>
    /// Gets the value(s) associated with the specified parameter name, encoded as a comma-separated list.
    /// </summary>
    /// <param name="name">The parameter name to retrieve the value for.</param>
    /// <param name="value">
    /// When this method returns, contains a comma-separated list of the value(s) associated with the specified
    /// parameter name, if the parameter exists; otherwise, <see langword="null" />. This parameter is passed
    /// uninitialized.
    /// </param>
    /// <returns>
    /// <see langword="true" /> if a parameter with the specified name exists; otherwise, <see langword="false" />.
    /// </returns>
    bool TryGetValue(string name, out string? value);

    /// <summary>
    /// Gets all the values associated with the specified parameter name.
    /// </summary>
    /// <param name="name">The parameter name to retrieve the value for.</param>
    /// <returns>
    /// An array of strings, where each element represents one value of the parameter. If the parameter does not exist,
    /// <see langword="null" /> is returned. If the parameter has no values, an empty array is returned.
    /// </returns>
    string?[]? GetValues(string name);

    /// <summary>
    /// Determines whether the collection contains a parameter with the specified name.
    /// </summary>
    /// <param name="parameterName">The parameter name to search for.</param>
    /// <returns><see langword="true" /> if the collection contains a parameter with the specified name; otherwise,
    /// <see langword="false" />.</returns>
    /// <remarks>
    /// This method performs a case-insensitive comparison of parameter names.
    /// </remarks>
    bool Contains(string parameterName);
}
