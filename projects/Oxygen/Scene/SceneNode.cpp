// ...existing code...
if (type_id == PerspectiveCamera::ClassTypeId()) {
  state.node_impl->AddComponent<PerspectiveCamera>(
      std::move(*static_cast<PerspectiveCamera *>(camera.get())));
} else if (type_id == OrthographicCamera::ClassTypeId()) {
  state.node_impl->AddComponent<OrthographicCamera>(
      std::move(*static_cast<OrthographicCamera *>(camera.get())));
} else {
  return false;
}
// ...existing code...
