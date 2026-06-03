
-- Load the engine's default scenario (contains default globals/camera/entities)
local scenario_handle = load_scenario("default_scenario")

-- Over-ride the background/clear-color of the scene.
clear_color(0, 32, 64)

-- Pre-load some materials to cycle through
local material_pool = {
    "default_material_wireframe", 
    "default_material_water", "default_material_grass", "default_material_cloth", "default_material_wood", 
    "default_material_metal", "default_material_glass", "default_material_skin", "default_material_rubber", 
    "default_material_ice", "default_material_stone", "default_material_lava","default_material_toon",
    "default_material_hologram", "default_material_iridescent", "default_material_plastic", "default_material_brick", 
    "default_material_leather", "default_material_gold", "default_material_snow", "default_material_dirt", 
    "default_material_neon" }

local material_handles = {}
for i, name in ipairs(material_pool) do
    material_handles[i] = tag_load(name, TAG_material)
end

local station_model_handle = import_model("station.glb")
local station_cbsp_handle = build_cbsp(station_model_handle)

if tag_get_block_count(scenario_handle, "entities") > 0 then
    local entity_handle = tag_get_block_field(scenario_handle, "entities", 1, "entity")
    if entity_handle and entity_handle >= 0 then
        tag_set_field(entity_handle, "model", station_model_handle)
        tag_set_field(entity_handle, "collision_bsp", station_cbsp_handle)
    end
end

local mat_timer = 0
local mat_index = 1
local mat_interval = 5 -- seconds between changes

-- seconds for all animations to complete their cycles
local cam_cycle_time = 15
local light_dir_cycle_time = 10
local light_col_cycle_time = 5

local total_time = 0
function update(dt) --called every game-tick.
    total_time = total_time + dt

    -- Orbit camera around (0,0,0)
    local cam_angle = (total_time / cam_cycle_time) * 2 * math.pi
    local radius = 10
    local height = 7
    local cam_x = radius * math.cos(cam_angle)
    local cam_z = radius * math.sin(cam_angle)
    camera_eye(cam_x, height, cam_z)
    camera_lookat(0, 0, 0, 0, 1, 0)

    -- Cycle light direction (orbit faster)
    local light_angle = (total_time / light_dir_cycle_time) * 8 * math.pi
    local light_x = math.sin(light_angle)
    local light_y = math.tan(light_angle) * math.tan(light_angle)
    local light_z = math.cos(light_angle)
    light_direction(light_x, light_y, light_z)

    -- Cycle light color (hue shift)
    local hue = (total_time / light_col_cycle_time) * 360  -- degrees
    local r = (math.sin(hue * math.pi / 180) + 1) 
    local g = (math.sin((hue + 120) * math.pi / 180) + 1)
    local b = (math.sin((hue + 240) * math.pi / 180) + 1)
    light_color(r , g , b )  -- brighter
     
     -- Cycle materials on the first entity's model
     mat_timer = mat_timer + dt
    if mat_timer >= mat_interval then
         mat_timer = 0
         mat_index = (mat_index % #material_handles) + 1
         local next_mat = material_handles[mat_index]

          -- 1. Check if scenario has entities before accessing the first one
         if tag_get_block_count(scenario_handle, "entities") > 0 then
             local ent_handle = tag_get_block_field(scenario_handle, "entities", 0, "entity")
             if ent_handle and ent_handle >= 0 then
                  -- 2. Get the model handle currently assigned to that entity
                 local model_handle = tag_get_field(ent_handle, "model")
                 if model_handle and model_handle >= 0 then
                      -- 3. Check if the model has a materials block and update it
                     if tag_get_block_count(model_handle, "materials") > 0 then
                         tag_set_block_field(model_handle, "materials", 0, "material", next_mat)
                     else
                         print("No materials block found in the model.")
                     end
                 else
                     print("No model found in the entity.")
                 end
                 
                local rigid_body_handle = tag_get_field(ent_handle, "rigid_body")
                tag_set_field(rigid_body_handle, "velocity", vec3(-5, 7, 0))
                tag_set_field(rigid_body_handle, "angular_velocity", vec3(1, 0, 0))
                tag_set_field(ent_handle, "position", vec3(5, 3, 3))
             end
         else
             print("No entities found in the scenario.")
         end
     end
end
