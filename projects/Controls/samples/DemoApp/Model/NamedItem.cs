// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Model;

using System;

public class NamedItem
{
    private string name;

    public NamedItem(string name)
    {
        ValidateName(name);
        this.name = name;
    }

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

    public static void ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ArgumentException(
                "a scene must have a name and it should not be only white spaces",
                nameof(name));
        }
    }
}
