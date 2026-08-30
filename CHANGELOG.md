# Changelog

## [0.4.0](https://github.com/EinDev/HNode/compare/v0.3.0...v0.4.0) (2026-08-30)


### Features

* CI-noise-normalized performance baseline ([1db27bb](https://github.com/EinDev/HNode/commit/1db27bb0ecf7f18b1ea16c5fd1f80ae34b95bf0a))
* implement MAVLinkDroneNetwork generator (Phase 1: transport) ([f79c4c6](https://github.com/EinDev/HNode/commit/f79c4c6f82c11d8b7bffa8a617f68aa3c7086e2b))
* implement TimeCodeExporter (MIDI Time Code -&gt; UDP) ([e5cbdd6](https://github.com/EinDev/HNode/commit/e5cbdd64fc5cc627f120b40a2f08fa6ad7e0367e))
* implement TwitchChat and OnTime generators ([1d168fd](https://github.com/EinDev/HNode/commit/1d168fde3a870547ba1a2abdcf60493d0c84cae4))
* surface errors in-app; persist show config across restarts ([44e1850](https://github.com/EinDev/HNode/commit/44e1850471409c122815a5ef244cb435a9c8f892))


### Bug Fixes

* move Spout SendFrame off the main thread ([6796045](https://github.com/EinDev/HNode/commit/6796045cb4f980f3bf379e699bbd39d3eeabcaf1))

## [0.3.0](https://github.com/EinDev/HNode/compare/v0.2.0...v0.3.0) (2026-08-30)


### Features

* accurate MIDI status and a perf-stats panel with a render-time graph ([934edad](https://github.com/EinDev/HNode/commit/934edad53b649901b9f0a73db1abae589e735448))


### Bug Fixes

* Spout output was uploading every frame to the GPU twice ([4f367d4](https://github.com/EinDev/HNode/commit/4f367d458dd1a96169386dbf3438a17df37715a1))
* taskbar/window icon not showing (only exe file icon worked) ([02ead5b](https://github.com/EinDev/HNode/commit/02ead5bd7b1f97f2d54801890a288196d76615e5))

## [0.2.0](https://github.com/EinDev/HNode/compare/v0.1.0...v0.2.0) (2026-08-30)


### Features

* prelim MIDIDMX support ([3a35f93](https://github.com/EinDev/HNode/commit/3a35f937035aa26d21283ce6e98895e62b785a37))
* prelim MIDIDMX support ([bffbf67](https://github.com/EinDev/HNode/commit/bffbf673836b75239f79da0bfba52eb0aba5016b))


### Bug Fixes

* **github-actions:** update github actions steps versions ([fd228d2](https://github.com/EinDev/HNode/commit/fd228d2feb4e23b310d968fa9403712c57180ad9))
* **VRSL:** apply outputConfig to VRSL deserializer ([06897fc](https://github.com/EinDev/HNode/commit/06897fc4832f57c3bbdcc5502309d8c2edce43b6))
