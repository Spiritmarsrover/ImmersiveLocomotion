# Immersive Locomotion — VRChat avatar (OneWheel board)

The overlay sends board state to VRChat over OSC so your avatar can show a
OneWheel board that appears while you ride and whose wheel spins with your
speed.

## OSC parameters (sent by `immersive_locomotion.exe`)

Enable the **OSC** tab in the overlay (sends to `127.0.0.1:9000` by default).

| Parameter        | Type          | Meaning                                   |
|------------------|---------------|-------------------------------------------|
| `IL_BoardActive` | Bool          | true while the board is active (riding)   |
| `IL_BoardSpeed`  | Float (-1..1) | signed wheel speed, normalized to max     |

Both are added to your avatar as **synced** expression parameters, so remote
players see the board and its spin.

## Setup

1. Import **VRChat SDK3 – Avatars** and **VRCFury** into your Unity project.
2. Copy `Editor/ILBoardSetup.cs` into an `Editor/` folder in your project.
3. Add your OneWheel model under your avatar: a **board root** object (the mesh
   that toggles on/off) with a **wheel** child transform that spins. Author it
   at your feet in the pose you ride.
4. **Tools ▸ Immersive Locomotion ▸ Setup Board**, assign the avatar, board
   root and wheel, and click **Generate**. This creates the animator + clips,
   and adds the two synced parameters. Board→feet **constraints are set up
   separately** — the script no longer touches them.
5. Add a **VRCFury ▸ Full Controller** to the avatar and point it at the
   generated `IL_Board.controller`. VRCFury merges the layers non-destructively.
6. Upload. In VRChat, enable OSC (it's on by default), enable the overlay's OSC
   tab, and ride — the board appears when active and the wheel spins with speed.

## Notes

- The board toggles via the `IL_BoardActive` bool driving the board root's
  active state; the wheel spins via a state whose speed is `IL_BoardSpeed`
  (signed, so it reverses when you ride backward). Tune the spin rate with the
  "Wheel deg/s at full speed" field.
- The foot constraint averages both feet (position + rotation), so the board
  sits at your stance midpoint and turns with your feet — direction is handled
  by your body, matching how the overlay steers.
- This is v1 (toggle + spin + feet constraint). Later: lean/pitch of the deck,
  tire squish, throttle/pushback effects, etc.
