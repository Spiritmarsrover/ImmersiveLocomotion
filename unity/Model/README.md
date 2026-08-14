# OneWheel board model

A OneWheel-style board model to use with the Immersive Locomotion board mode.
Provided under the repository's MIT license — free to use, modify, and share.

## Contents
- `Wheeler.fbx` — the board mesh. The board body and the wheel are separate
  transforms, so the wheel can spin independently (the setup tool animates it).
- `textures/` — the baked textures plus `Board.mat` and `Wheel.mat`.

## Shader dependency
The materials use **Mochie's Standard shader**
(https://github.com/MochiesCode/Mochie-Unity-Shaders), a free VRChat shader.
If you don't have it installed the materials will show up pink — either import
Mochie's shaders, or reassign any shader you like (the textures are included).

## Use
1. Copy this `Model/` folder into your project's `Assets/`.
2. Drag `Wheeler.fbx` into your avatar, position it at your feet in your ride
   pose.
3. Run **Tools ▸ Immersive Locomotion ▸ Setup Board** and assign the board
   root + wheel (see `../README.md`).
