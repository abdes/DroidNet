//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "LoguruWrapper.h"

void Oxygen::Interop::Logging::cdecl_log_handler(void* user_data, const loguru::Message& message)
{
  // Prepare a MessageWrapper from the loguru message
  MessageWrapper^ log_message = gcnew MessageWrapper();
  log_message->Verbosity = message.verbosity;
  log_message->Filename = gcnew System::String(message.filename);
  log_message->Line = message.line;
  log_message->Preamble = gcnew System::String(message.preamble);
  log_message->Indentation = gcnew System::String(message.indentation);
  log_message->Prefix = gcnew System::String(message.prefix);
  log_message->Message = gcnew System::String(message.message);

  // Call the _handle_log delegate in the library wrapper ref
  // Cast the opaque pointer to the hnative_handle ref (for readability)
  gcroot<Loguru^>& native_handle = *((gcroot<Loguru^>*)user_data);
  native_handle->_handle_log(log_message);
}
