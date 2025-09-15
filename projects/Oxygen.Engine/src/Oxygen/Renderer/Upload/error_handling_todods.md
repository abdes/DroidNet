# Upload Module Error Handling Issues - Tracking Table

| Status | Description | Class/Function |
|--------|-------------|----------------|
| ✅ | Returns default UploadTicket{} on invalid buffer descriptor | `UploadCoordinator::SubmitBuffer()` |
| ✅ | Returns default UploadTicket{} on invalid texture2D descriptor | `UploadCoordinator::SubmitTexture2D()` |
| ✅ | Returns default UploadTicket{} on invalid texture3D descriptor | `UploadCoordinator::SubmitTexture3D()` |
| ✅ | Returns default UploadTicket{} on invalid textureCube descriptor | `UploadCoordinator::SubmitTextureCube()` |
| ✅ | Returns default UploadTicket{} on unknown upload kind | `UploadCoordinator::Submit()` |
| ⏳ | Returns empty Allocation{} on buffer creation failure | `SingleBufferStaging::Allocate()` |
| ⏳ | Returns empty Allocation{} on mapping failure | `SingleBufferStaging::Allocate()` |
| ⏳ | Returns empty Allocation{} on capacity failure | `RingBufferStaging::Allocate()` |
| ⏳ | No error code when Graphics::CreateBuffer() fails | `SingleBufferStaging::EnsureCapacity_()` |
| ⏳ | No error code when Buffer::Map() fails | `SingleBufferStaging::Map_()` |
| ✅ | Default UploadTicket looks valid but represents failure | `Types.h::UploadTicket` |
| ✅ | TicketId starts at 0 which conflicts with invalid indicator | `Types.h::TicketId` |
| ⏳ | No IsValid() method to check allocation validity | `StagingProvider::Allocation` |
| ✅ | UploadError enum missing validation failure codes | `Types.h::UploadError` |
| ✅ | No error propagation in SubmitMany batch failures | `UploadCoordinator::SubmitMany()` |
| ✅ | Silent failure when plan.total_bytes == 0 | `UploadCoordinator::SubmitMany()` |
| ✅ | Silent failure when plan.buffer_regions.empty() | `UploadCoordinator::SubmitMany()` |
| ⏳ | No staging allocation failure handling in batched uploads | `UploadCoordinator::SubmitMany()` |
| ⏳ | CommandRecorder acquisition can fail silently | `UploadCoordinator::SubmitBuffer()` |
| ⏳ | Queue::Signal() can fail but not checked | `UploadCoordinator::SubmitBuffer()` |
| ✅ | Producer function failure only logged, not propagated | `UploadCoordinator::SubmitBuffer()` |
| ⏳ | Graphics context validity not checked before operations | `UploadCoordinator::Submit()` |
| ✅ | UploadPlanner can return invalid plans without error indication | `UploadPlanner::PlanBuffers()` |
| ✅ | Format validation missing for texture uploads | `UploadCoordinator::SubmitTexture2D()` |
| ⏳ | Resource state transition can fail silently | `UploadCoordinator::SubmitBuffer()` |
| ⏳ | Command recording can fail but errors not propagated | `UploadCoordinator::SubmitBuffer()` |
