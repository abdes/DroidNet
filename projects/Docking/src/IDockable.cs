// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Globalization;

public interface IDockable : IDisposable
{
    event Action? OnDisposed;

    string Id { get; }

    string Title { get; }

    string MinimizedTitle { get; }

    string TabbedTitle { get; }

    Width PreferredWidth { get; set; }

    Height PreferredHeight { get; set; }

    object? ViewModel { get; }

    IDock? Owner { get; set; }

    bool IsActive { get; set; }

    public class Width : Length
    {
        public Width(string? value = null)
            : base(value)
        {
        }

        public Width(double value)
            : base(value)
        {
        }
    }

    public class Height : Length
    {
        public Height(string? value = null)
            : base(value)
        {
        }

        public Height(double value)
            : base(value)
        {
        }
    }

    public abstract class Length
    {
        protected Length(double value) => this.Value = double.Round(value).ToString(CultureInfo.InvariantCulture);

        protected Length(string? value) => this.Value = value;

        public string? Value { get; }
    }
}
