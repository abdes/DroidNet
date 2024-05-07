// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

using Microsoft.Extensions.Options;

public interface IWritableOptions<out T> : IOptions<T>
    where T : class, new()
{
    void Update(Action<T> applyChanges);
}
