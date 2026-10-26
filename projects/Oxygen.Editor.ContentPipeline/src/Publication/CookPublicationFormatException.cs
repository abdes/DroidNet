// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>
/// The project's cooked content was published in a format this editor does not read. Cooking the whole project, on the
/// user's request, publishes it again in the current format.
/// </summary>
public sealed class CookPublicationFormatException : IOException
{
    /// <summary>Initializes a new instance of the <see cref="CookPublicationFormatException"/> class.</summary>
    public CookPublicationFormatException()
        : base("The project's cooked content was published in a format this editor does not read. Cook the project to upgrade it.")
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookPublicationFormatException"/> class.</summary>
    /// <param name="message">What is outdated and how to upgrade it.</param>
    public CookPublicationFormatException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookPublicationFormatException"/> class.</summary>
    /// <param name="message">What is outdated and how to upgrade it.</param>
    /// <param name="innerException">The failure that revealed it.</param>
    public CookPublicationFormatException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance of the <see cref="CookPublicationFormatException"/> class.</summary>
    /// <param name="version">The format the publication was written in, or null when it names none.</param>
    /// <param name="current">The format this editor reads and writes.</param>
    internal CookPublicationFormatException(int? version, int current)
        : base(
            $"The project's cooked content was published in format {version?.ToString(CultureInfo.InvariantCulture) ?? "unknown"}, "
            + $"and this editor uses format {current.ToString(CultureInfo.InvariantCulture)}. Cook the project to upgrade it.")
    {
        this.Version = version;
    }

    /// <summary>Gets the format the publication was written in, or null when it names none.</summary>
    public int? Version { get; }
}
