// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Model;

/// <summary>
/// Represents an item with a name.
/// </summary>
public class NamedItem
{
    private string name;

    /// <summary>
    /// Initializes a new instance of the <see cref="NamedItem"/> class.
    /// </summary>
    /// <param name="name">The name of the item.</param>
    /// <exception cref="ArgumentException">Thrown when the name is null, empty, or consists only of white spaces.</exception>
    protected NamedItem(string name)
    {
        ValidateName(name);
        this.name = name;
    }

    /// <summary>
    /// Gets or sets the name of the item.
    /// </summary>
    /// <exception cref="ArgumentException">Thrown when the name is null, empty, or consists only of white spaces.</exception>
    public string Name
    {
        get => this.name;
        set
        {
            if (this.name.Equals(value, StringComparison.Ordinal))
            {
                return;
            }

            ValidateName(value);
            this.name = value;
        }
    }

    /// <summary>
    /// Validates the given name.
    /// </summary>
    /// <param name="name">The name to validate.</param>
    /// <exception cref="ArgumentException">Thrown when the name is null, empty, or consists only of white spaces.</exception>
    public static void ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ArgumentException(
                "A scene must have a name and it should not be only white spaces",
                nameof(name));
        }
    }
}
