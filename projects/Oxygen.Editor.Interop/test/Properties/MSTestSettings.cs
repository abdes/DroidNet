// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

/*
 * We can only have one engine configured, and potentially running at a time.
 * Therefore, we should never run tests in parallel.
 */

[assembly: DoNotParallelize]
