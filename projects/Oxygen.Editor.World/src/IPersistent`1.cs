// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World;

/// <summary>
/// Represents an object that can be hydrated from and dehydrated to a data transfer object.
/// </summary>
/// <typeparam name="TData">The type of the data transfer object.</typeparam>
public interface IPersistent<TData>
{
    /// <summary>
    /// Hydrates this object from the specified data transfer object.
    /// </summary>
    /// <param name="data">The data transfer object containing the state to load.</param>
    public void Hydrate(TData data);

    /// <summary>
    /// Dehydrates this object to a data transfer object.
    /// </summary>
    /// <returns>A data transfer object containing the current state of this object.</returns>
    public TData Dehydrate();
}
