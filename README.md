# Moonaut Engine

A lightweight game engine written in C with Lua scripting support. Designed for simplicity, featuring a tag-based asset system, real-time physics, and multiple software-rendering shading modes with ~20 effects.

<img width="320" height="211" alt="Image" src="https://github.com/user-attachments/assets/449c1bc3-0391-4cfd-8725-22538f95d06b" />
<img width="256" height="204" alt="Image" src="https://github.com/CollinMcKinney/Moonaut/blob/master/flowchart.png" />

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

### CMake, Ninja, & Clang
Project has been moved to CMake. The Clang/Ninja presets are what I've been using for development. I've included some placeholder presets for GCC, VisualStudio, and XCode, but they are largely untested.

All recent testing/development has been done under Linux, Windows is likely broken. MacOS has never been tested.

List the presets:
```bash
cmake --list-presets
```
Select a preset for your system, here we will use the `clang-release` preset:
```bash
cmake --preset clang-release
```
Build the preset:
```bash
cmake --build --preset clang-release
```
And then to run the executable:
```bash
./build/clang-release/Moonaut
```