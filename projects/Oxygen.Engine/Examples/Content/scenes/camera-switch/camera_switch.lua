-- Demonstrates switching the scene's active camera and input contexts from a
-- script. C cycles between the scene's cameras while playing; Tab opens a
-- "menu" by swapping the play context for the menu context, and Tab again
-- swaps them back. While the menu is open C does nothing, because its context
-- is inactive.
local script = oxygen.script {
  params = {
    first_camera = oxygen.param.node {
      description = "Camera the scene starts with" },
    second_camera = oxygen.param.node {
      description = "Camera C switches to" },
    play_context = oxygen.param.asset { kind = "input_mapping_context",
      description = "Context active while playing" },
    menu_context = oxygen.param.asset { kind = "input_mapping_context",
      description = "Context active while the menu is open" },
    next_camera = oxygen.param.asset { kind = "input_action",
      description = "Action that switches camera" },
    open_menu = oxygen.param.asset { kind = "input_action",
      description = "Action that opens the menu" },
    close_menu = oxygen.param.asset { kind = "input_action",
      description = "Action that closes the menu" },
  },
}

local input = oxygen.input
local scene = oxygen.scene
local log = oxygen.log

local function switch_camera(self)
  local current = scene.active_camera()
  local target = self.params.first_camera
  if current ~= nil and target ~= nil
    and current:get_name() == target:get_name() then
    target = self.params.second_camera
  end
  if target ~= nil and scene.set_active_camera(target) then
    log.info("camera_switch.lua: viewing through " .. target:get_name())
  end
end

local function set_menu(self, open)
  local from = open and self.params.play_context or self.params.menu_context
  local to = open and self.params.menu_context or self.params.play_context
  input.deactivate_context(from)
  input.activate_context(to)
  log.info(open and "camera_switch.lua: menu opened"
    or "camera_switch.lua: menu closed")
end

function script.on_start(self)
  input.on_action(self.params.next_camera, input.edges.triggered, function()
    switch_camera(self)
  end)
  input.on_action(self.params.open_menu, input.edges.triggered, function()
    set_menu(self, true)
  end)
  input.on_action(self.params.close_menu, input.edges.triggered, function()
    set_menu(self, false)
  end)
end

return script
