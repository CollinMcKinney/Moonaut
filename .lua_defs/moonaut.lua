---@meta

---@alias real number
---@alias body_id integer
---@alias shading_mode integer

---@class vec2
---@field x real
---@field y real

---@class vec3
---@field x real
---@field y real
---@field z real

---@class vec4
---@field x real
---@field y real
---@field z real
---@field w real

---@param x real
---@param y real
---@return vec2
function vec2(x, y) end

---@param x real
---@param y real
---@param z real
---@return vec3
function vec3(x, y, z) end

---@param x real
---@param y real
---@param z real
---@param w real
---@return vec4
function vec4(x, y, z, w) end

---@param x number
---@param y number
---@param z number
---@param radius number
---@return body_id
function sphere(x, y, z, radius) end

---@overload fun(center: vec3, radius: number): body_id

---@param x number
---@param y number
---@param z number
---@param half_x number
---@param half_y number
---@param half_z number
---@return body_id
function box(x, y, z, half_x, half_y, half_z) end

---@overload fun(center: vec3, half_ext: vec3): body_id

---@param x number
---@param y number
---@param z number
---@param half_x number
---@param half_y number
---@param half_z number
---@param mass_value number
---@return body_id
function dynamic_box(x, y, z, half_x, half_y, half_z, mass_value) end

---@overload fun(center: vec3, half_ext: vec3, mass_value: number): body_id

---@param x number
---@param y number
---@param z number
function gravity(x, y, z) end

---@overload fun(v: vec3)
function clear() end

---@param x number
---@param y number
---@param z number
function camera_eye(x, y, z) end

---@overload fun(v: vec3)

---@param cx number
---@param cy number
---@param cz number
---@param ux number
---@param uy number
---@param uz number
function camera_lookat(cx, cy, cz, ux, uy, uz) end

---@overload fun(center: vec3, up: vec3)

---@param degrees number
function camera_fov(degrees) end

---@param dirx number
---@param diry number
---@param dirz number
---@param colr number
---@param colg number
---@param colb number
---@param ambr number
---@param ambg number
---@param ambb number
function light(dirx, diry, dirz, colr, colg, colb, ambr, ambg, ambb) end

---@overload fun(dir: vec3, color: vec3, ambient: vec3)

---@param x number
---@param y number
---@param z number
function light_direction(x, y, z) end

---@overload fun(v: vec3)

---@param r number
---@param g number
---@param b number
function light_color(r, g, b) end

---@overload fun(v: vec3)

---@param r number
---@param g number
---@param b number
function light_ambient(r, g, b) end

---@overload fun(v: vec3)

---@param mode shading_mode
function shading_mode(mode) end

---@param r number
---@param g number
---@param b number
function clear_color(r, g, b) end

---@overload fun(v: vec3)

---@param on number|boolean
function pause_physics(on) end

---@param hz number
function physics_rate(hz) end

---@param name string
---@return number
function load_scenario(name) end

---@param name string
---@param group number
---@return number
function tag_load(name, group) end

---@param handle number
---@param field_name string
---@return any
function tag_get_field(handle, field_name) end

---@param handle number
---@param field_name string
---@param value any
function tag_set_field(handle, field_name, value) end

---@param handle number
---@param block_name string
---@return number
function tag_get_block_count(handle, block_name) end

---@param handle number
---@param block_name string
---@param index number
---@param field_name string
---@return any
function tag_get_block_field(handle, block_name, index, field_name) end

---@param handle number
---@param block_name string
---@param index number
---@param field_name string
---@param value any
function tag_set_block_field(handle, block_name, index, field_name, value) end

---@param handle number
---@return function
function tag_get_script(handle) end

---@param id body_id
---@param x number
---@param y number
---@param z number
function velocity(id, x, y, z) end

---@overload fun(id: body_id, v: vec3)

---@param id body_id
---@param x number
---@param y number
---@param z number
function impulse(id, x, y, z) end

---@overload fun(id: body_id, v: vec3)

---@param id body_id
---@param value number
function mass(id, value) end

---@param id body_id
---@param value number
function restitution(id, value) end

---@param id body_id
---@param value number
function friction(id, value) end

---@param dt number
function update(dt) end

SHADE_WIREFRAME = 0
SHADE_FLAT = 1
SHADE_GOURAUD = 2
SHADE_PHONG = 3

TAG_material = 0  -- actual values not important for LSP
TAG_model = 0
TAG_entity = 0
TAG_rigid_body = 0
TAG_scenario = 0
TAG_globals = 0
TAG_camera = 0
TAG_lua_script = 0
