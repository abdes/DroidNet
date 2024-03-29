// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Hosting.Generators.Utils;

using Microsoft.CodeAnalysis;

internal static class TypeSymbolExtension
{
    /// <summary>
    /// Checks whether a given <see cref="ITypeSymbol" /> implements the specified type.
    /// </summary>
    /// <param name="typeSymbol">The target <see cref="ITypeSymbol" /> instance to check.</param>
    /// <param name="name">The full name of the interface to check for.</param>
    /// <returns>
    /// Whether <paramref name="typeSymbol" /> implements the interface with the specified full <paramref name="name" />.
    /// </returns>
    public static bool ImplementsInterface(this ITypeSymbol typeSymbol, string name)
        => typeSymbol.Interfaces.Any(x => string.Equals(x.ToDisplayString(), name, StringComparison.Ordinal));
}
