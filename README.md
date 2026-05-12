# Moonaut Engine

A lightweight game engine written in C with Lua scripting support. Designed for simplicity, featuring a tag-based asset system, real-time physics, and multiple software-rendering shading modes with ~20 effects.

<img width="786" height="750" alt="Image" src="https://github.com/user-attachments/assets/9f5fa635-fe84-4186-8e8e-c30965d46536" />
<img width="720" height="370" alt="Image" src="https://github.com/user-attachments/assets/081f3619-32c8-4099-8357-d80f67abcf0c" />
<img width="305" height="347" alt="Image" src="https://github.com/user-attachments/assets/c39a06d4-004c-4d78-ad64-47964a27c27f" />

## Features

- **Tag-Based Asset System**: Reflective data structures for materials, models, entities, and scenarios
- **Physics Simulation**: Built-in rigid body dynamics with collision detection
- **Rendering Pipeline**: Multiple shading modes (Wireframe, Flat, Gouraud, Phong) with about ~20 effects that can be combined and individually tweaked.
- **Lua Scripting**: Full Lua integration for gameplay logic and entity manipulation
- **Hot Reloading**: Live script updates during development

## How It Works

Moonaut Engine operates on a tag-based architecture where all game assets—materials, models, entities, and scenarios—are defined as structured data with reflection capabilities. This allows for efficient serialization and runtime manipulation of game objects. The engine uses a scenario system to organize entities in a world, each with optional physics bodies and visual representations.

At its core, the engine runs a fixed-timestep physics simulation integrated with a software rasterization renderer. Entities can have rigid bodies for collision and dynamics, rendered using various shading techniques from simple wireframes to advanced Phong lighting. Lua scripting provides the glue, allowing developers to create entities, apply forces, control cameras, and manipulate the scene in real-time through a comprehensive API that exposes both low-level physics and high-level tag operations.

## Building

### Windows
```bash
./build.sh
```
