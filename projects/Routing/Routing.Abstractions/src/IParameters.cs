// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Routing;

/// <summary>
/// Represents a collection of URL parameters, supporting both matrix parameters in path segments
/// and query parameters in URLs.
/// </summary>
/// <remarks>
/// <para>
/// This interface provides a flexible way to handle URL parameters, which can be either matrix
/// parameters embedded in path segments or query parameters appended to the URL. Parameter names
/// are case-insensitive for lookup purposes, and values follow RFC 3986 encoding rules.
/// </para>
/// <para>
/// Multiple values for the same parameter can be represented in several ways:
/// <list type="definition">
///   <item>
///     <term>Parameter without value</term>
///     <description>Just the name: <c>left</c> (stored as name with <see langword="null"/> value)</description>
///   </item>
///   <item>
///     <term>Parameter with empty value</term>
///     <description>Name with equals: <c>name=</c> (stored as name with empty string)</description>
///   </item>
///   <item>
///     <term>Single value</term>
///     <description>Basic form: <c>w=300</c></description>
///   </item>
///   <item>
///     <term>Multiple values as list</term>
///     <description>Comma-separated: <c>user=John,Peter</c></description>
///   </item>
///   <item>
///     <term>Multiple values as repetition</term>
///     <description>Repeated parameters: <c>user=John&amp;user=Peter</c></description>
///   </item>
/// </list>
/// </para>
/// <para>
/// The interface provides two methods for accessing parameter values:
/// <list type="definition">
///   <item>
///     <term><see cref="TryGetValue"/></term>
///     <description>Returns the raw string value as stored. For example, with either
///     <c>user=John,Peter</c> or <c>user=John&amp;user=Peter</c>, returns "John,Peter"</description>
///   </item>
///   <item>
///     <term><see cref="GetValues"/></term>
///     <description>Returns individual values as an array. For example, with either
///     <c>user=John,Peter</c> or <c>user=John&amp;user=Peter</c>, returns ["John", "Peter"]</description>
///   </item>
/// </list>
/// </para>
/// <para>
/// The default implementation (<see cref="Parameters"/>) stores multiple values as
/// comma-separated lists internally, but clients should not rely on this detail as
/// other implementations may choose different storage strategies.
/// </para>
///
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// var parameters = new Parameters();
/// parameters.Add("user", "John");
/// parameters.Add("user", "Peter");
///
/// // Using TryGetValue
/// if (parameters.TryGetValue("user", out var value))
/// {
///     Console.WriteLine(value); // Outputs: "John,Peter"
/// }
///
/// // Using GetValues
/// var values = parameters.GetValues("user");
/// foreach (var val in values)
/// {
///     Console.WriteLine(val); // Outputs: "John" and "Peter" on separate lines
/// }
/// ]]></code>
/// </example>
///
/// <para><strong>Implementation Guidelines</strong></para>
/// <para>
/// When implementing this interface, ensure that parameter names are treated case-insensitively and
/// that multiple values are handled correctly, either as comma-separated lists or repeated parameters.
/// </para>
/// </remarks>
public interface IParameters : IEnumerable<Parameter>, IEquatable<IParameters>
{
    /// <summary>
    /// Gets the number of distinct parameter names in the collection.
    /// </summary>
    public int Count { get; }

    /// <summary>
    /// Gets a value indicating whether the collection contains any parameters.
    /// </summary>
    public bool IsEmpty { get; }

    /// <summary>
    /// Gets the value(s) associated with the specified parameter name as a single string.
    /// </summary>
    /// <param name="name">The parameter name to retrieve (case-insensitive).</param>
    /// <param name="value">
    /// When this method returns, contains either:
    /// <list type="bullet">
    ///   <item><see langword="null"/> if the parameter exists without a value</item>
    ///   <item>the single value if the parameter has one value</item>
    ///   <item>a comma-separated list of values if the parameter has multiple values</item>
    /// </list>
    /// </param>
    /// <returns><see langword="true"/> if the parameter exists; otherwise, <see langword="false"/>.</returns>
    public bool TryGetValue(string name, out string? value);

    /// <summary>
    /// Gets all values associated with the specified parameter name as separate strings.
    /// </summary>
    /// <param name="name">The parameter name to retrieve (case-insensitive).</param>
    /// <returns>
    /// <list type="bullet">
    ///   <item><see langword="null"/> if the parameter doesn't exist</item>
    ///   <item>empty array if the parameter exists without values</item>
    ///   <item>array of individual values, split from either comma-separated lists or multiple parameter occurrences</item>
    /// </list>
    /// </returns>
    public string?[]? GetValues(string name);

    /// <summary>
    /// Determines whether a parameter with the specified name exists.
    /// </summary>
    /// <param name="parameterName">The parameter name to check (case-insensitive).</param>
    /// <returns><see langword="true"/> if the parameter exists; otherwise, <see langword="false"/>.</returns>
    public bool Contains(string parameterName);
}
