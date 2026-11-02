# Asce

Asce is a work-in-progress real-time strategy prototype built on Unreal Engine 5 and Mass. It explores how far a data-driven, entity-based architecture can be pushed for RTS gameplay: large unit counts, modular units, pathfinding at scale, and networked combat.

The project is under active development by a solo developer. It is not a finished game and the API, data formats, and features change frequently.

## Highlights

- **Mass + StateTree units** — per-unit-type behavior trees, timeline-driven actions, server-anchored state, and a spatial grid for queries
- **Flow-field pathfinding** — Eikonal-based solver with per-team cost layering, integrated with steering and crowd movement
- **Combat** — skill/attack definitions in data tables, cue-driven animation, fixed skill slots, and upstream/downstream attribute replication
- **Unit modularization** — a unit editor with validation, so units are assembled from data instead of one-off Blueprints
- **Buildings** — placement, destruction, and grid interaction sharing the same entity pipeline as units
- **Vision / fog of war** — implemented in a custom HLSL shader (`Shaders/AsceFog.usf`)
- **Networking** — Mass entity replication, server-authoritative movement, and replicated gameplay state
- **Cheat / debug console** — a Slate console with registry-driven commands and per-player permissions
- **AngelScript layer** — gameplay logic prototyped in `.as` files (`Script/`) on top of the C++ framework

## Tech stack

- Unreal Engine 5 (built from a custom source build with AngelScript support)
- C++ modules: `AsceGame` (runtime), `AsceEditor` (editor tooling), `AsceShaders` (shader declarations)
- Mass Entity, StateTree, Gameplay Ability System, CommonUI / CommonGame, Replication Graph, Enhanced Input
- Custom HLSL under `Shaders/`
- AngelScript for gameplay scripting under `Script/`

## Repository layout

| Path | Contents |
| --- | --- |
| `Source/` | C++ gameplay, editor tooling, and shader modules |
| `Script/` | AngelScript gameplay scripts and examples |
| `Config/` | Project configuration (engine, game, input, Mass, tags) |
| `Shaders/` | Custom HLSL shaders |
| `Asce.uproject` | Project descriptor |

## Requirements

- **Engine:** Unreal Engine 5 from a custom source build. `Asce.uproject` stores an `EngineAssociation` GUID, so the project must be opened with that engine build rather than a launcher installation.
- **Asset repository:** assets are not tracked here. `Content/` is versioned separately with Diversion. A fresh clone contains code and configuration only and will not run without those assets.
- **Plugins:** third-party plugins are excluded by `.gitignore` and must be installed separately.

## Building

1. Clone the repository.
2. Generate project files from `Asce.uproject` with the matching engine build.
3. Build the `AsceEditor` target.
4. Open `Asce.uproject` in the editor.

Because `Content/` is not part of this repository, the project needs the corresponding asset snapshot to start up.

## License

MIT — see [LICENSE](LICENSE).

Portions of the framework are adapted from Epic Games' Lyra sample and remain subject to the Unreal Engine EULA. Third-party plugins and libraries keep their own licenses.
