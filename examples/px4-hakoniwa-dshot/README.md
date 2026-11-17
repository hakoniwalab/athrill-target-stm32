# examples/px4-hakoniwa-dshot

PX4 on the emulated Pixhawk 4 with DShot600 ESCs, their telemetry and a motor stop: [docs/dshot.md](../../docs/dshot.md).

For a managed browser demo, use the
[`athrill-pixhawk-px4-motor-fault-viewer`](https://github.com/hakoniwalab/hakoniwa-business-pack/blob/main/recipes/examples/athrill-pixhawk-px4-motor-fault-viewer.yaml)
Business Pack Recipe. It prepares firmware state outside this source tree,
starts Athrill, the PDU web bridge and Three.js viewer through one owned Launcher
session, and then runs `fly-motor-fault.py` as the observable scenario.

`run.sh` accepts `ATHRILL_WORK_DIR` for generated F-RAM, SD-card, configuration
and first-boot logs. `ATHRILL_PREPARE_ONLY=1` prepares that state and exits before
joining Hakoniwa.
