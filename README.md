# DF and orbs in Pause Menu

A Geometry Dash Geode mod that adds useful level information to the pause menu.

## Features

- Displays the current level's difficulty in the pause menu.
- For verified levels, displays the orbs and diamonds collected in the level.
- Designed for Geode on iOS/iPadOS first.

## Project status

**Early development.** The repository is being built incrementally, with every milestone tested directly on an iPad using Geometry Dash and Geode.

## Development workflow

The project is developed primarily with AI assistance. The main development device is an iPad Pro M5, so iOS builds are produced remotely through GitHub Actions using a macOS runner.

The intended workflow is:

1. Make or review changes in this repository.
2. Build the iOS Geode mod with GitHub Actions.
3. Download the generated `.geode` package.
4. Install it into Geode on the iPad.
5. Test it in Geometry Dash.
6. Use logs and test results to guide the next iteration.

## Building

The GitHub Actions workflow will build the mod for iOS using Geode's official build tooling.

A successful workflow run will produce a `.geode` artifact for testing.

## Planned milestones

- [ ] Verify the basic Geode iOS build pipeline.
- [ ] Load the mod successfully in Geometry Dash.
- [ ] Hook the pause menu.
- [ ] Display the current level difficulty.
- [ ] Determine the correct game data for collected orbs.
- [ ] Determine the correct game data for collected diamonds.
- [ ] Detect the required verified-level state.
- [ ] Display collected orbs and diamonds only when appropriate.
- [ ] Polish the pause-menu layout.
- [ ] Test different level types and gameplay states.
- [ ] Prepare the first public release.

## Distribution

The finished mod is intended to be distributed through GitHub as a `.geode` package. A submission to the Geode Mod Index may be considered once the mod is stable.

## Credits

Built with the [Geode SDK](https://github.com/geode-sdk/geode) and its modding ecosystem.
