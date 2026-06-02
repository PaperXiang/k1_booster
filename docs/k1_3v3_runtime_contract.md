# K1 3v3 Runtime Contract

This note records the active runtime contract for the 3v3 demo. It is intended
to prevent tuning against stale or unused topics/configuration.

## Main perception topics

### `/booster_vision/detection`

- Producer: `vision_node`
- Consumer: `brain_node`
- Message: `vision_interface/msg/Detections`
- `header.stamp`: source image timestamp
- `header.frame_id`: `base_link`
- `DetectedObject.position`: robot base frame, meters, x forward and y left
- `DetectedObject.position_projection`: projection fallback in robot base frame
- `DetectedObject.confidence`: detector confidence scaled to 0-100 for brain
- `DetectedObject.position_confidence`:
  - `1`: projection fallback
  - `2`: measured by ground plane or object depth
  - `3`: reserved for predicted position; current vision output keeps
    `position` as the measured position instead of publishing the extrapolated
    value as a direct observation

### `/booster_vision/line_segments`

- Producer: `vision_node`
- Consumer: `brain_node`
- Message: `vision_interface/msg/LineSegments`
- `header.stamp`: source image timestamp
- `header.frame_id`: `base_link`
- `coordinates`: robot base frame line endpoints, 4 floats per segment
- `coordinates_uv`: image-space line endpoints, 4 floats per segment
- Brain ignores malformed messages where either array is not a multiple of 4 or
  where the two arrays do not contain the same number of floats.

### `/booster_vision/ball`

- Producer: `vision_node`
- Current brain main path: not consumed
- Treat this as a debug/simple export unless a dedicated consumer is added.
  Brain uses Ball objects inside `/booster_vision/detection`.

## Ball motion prediction

- Active implementation: `src/ball_motion_predictor`
- Legacy predictor: `src/brain/include/posProjector.h` is deprecated and not in
  the current 3v3 main path.
- Current strategy: velocity-only short-horizon prediction with speed gating.
- Vision logs predicted position for diagnostics, but keeps published
  `DetectedObject.position` as the measured position so brain does not chase an
  extrapolated point as if it were a direct observation.

## 3v3 team size

- `src/brain/config/config.yaml` uses `game.number_of_players: 3` for the 3v3
  demo.
- Per-robot `player_id` and `player_role` should still be checked on each robot
  before a run.
