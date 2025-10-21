// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;

namespace DroidNet.Config.Tests.Helpers;

/// <summary>
/// Interface for test settings with validation errors.
/// </summary>
public interface ITestSettingsWithValidation : INotifyPropertyChanged
{
    [Required]
    public string? RequiredField { get; set; }

    [Range(1, 10)]
    public int OutOfRangeValue { get; set; }

    [EmailAddress]
    public string? InvalidEmail { get; set; }
}
