// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Mvvm.Generators;

/// <summary>
/// An attribute that can be used to decorate a view class with metadata to
/// wire it to a specific ViewModel class.
/// </summary>
/// <remarks>
/// The source generator will augment the View class, implementing the
/// `IViewFor{T}` interface and adding a dependency property for the
/// `ViewModel` property.
/// </remarks>
/// <param name="viewModelType">
/// an expression of the form `typeof(T)` where `T` is the ViewModel class
/// name.
/// </param>
[AttributeUsage(AttributeTargets.Class)]
public sealed class ViewModelAttribute(Type viewModelType) : Attribute
{
    /// <summary>
    /// Gets the value of the attribute's parameter containing the ViewModel
    /// class.
    /// </summary>
    /// <value>The ViewModel class.</value>
    public Type ViewModelType { get; } = viewModelType;
}
