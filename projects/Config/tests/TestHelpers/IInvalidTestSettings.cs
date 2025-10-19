// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;

namespace DroidNet.Config.Tests.TestHelpers;

/// <summary>
/// Interface for test settings with validation errors.
/// </summary>
public interface IInvalidTestSettings : INotifyPropertyChanged
{
    [Required]
    string? RequiredField { get; set; }

    [Range(1, 10)]
    int OutOfRangeValue { get; set; }

    [EmailAddress]
    string? InvalidEmail { get; set; }
}
