# Oxygen Editor Document Storage Layer

## Error handling and exceptions

The design philosophy is to minimize the types of exceptions reported. Most of
the time, there is very little the UI can do other than reporting the error
message to the user. There are however situation where the UI can be intelligent
and transparently handle the error or offer a smarter choice to the user. These
scenarios include:

- If a storage operation is attempted on an item, but the item was not found at
the time the operation was exceuted, an ItemNotFoundException can be reported.
In such case, the UI can ignore the failed operation and update itself to remove
the item from display.

- If a storage operation involving creating, copying or moving an item and
another item with the same name exists at the target location, a
TargetExistsException could be thrown. In such case, the UI can offer a choice
to the user to overwrite, rename or cancel.

- Any other error should simply be encapsulated inside a StorageException, with
the original exception set as an InnerException.

## Encoding in text documents

All documents that have text content (such as JSON files) use the "UTF-8" encoding.
