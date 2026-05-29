# Protocol-level P-OLSR implementation notes

This directory keeps an independent P-OLSR protocol copy under `scratch/P-OLSR`.
The native ns-3 `src/olsr` module is not modified.

## Code organization

- `polsr-module/model/polsr-repositories.h`
  - Extends link and topology tuples with prediction fields.
  - Adds `NodeMotionTuple` for longitude, latitude, and altitude learned from HELLO messages.
- `polsr-module/model/polsr-state.h`
- `polsr-module/model/polsr-state.cc`
  - Adds storage and update APIs for motion, link prediction, and topology prediction state.
- `polsr-module/model/polsr-header.h`
- `polsr-module/model/polsr-header.cc`
  - Extends HELLO with optional P-OLSR GPS fields: longitude, latitude, and altitude.
  - Adds per-neighbor HELLO link-quality fields: forward receiving ratio, reverse receiving ratio, and relative speed.
  - Extends TC with optional per-neighbor speed-weighted ETX, receiving ratios, and relative speed.
  - Keeps old-format decode paths for messages without P-OLSR extension fields.
- `polsr-module/model/polsr-routing-protocol.h`
- `polsr-module/model/polsr-routing-protocol.cc`
  - Adds P-OLSR attributes:
    - `EnablePrediction`
    - `UsePredictionForRouting`
    - `UsePredictionForMpr`
    - `PredictionAlpha`
    - `PredictionBeta`
    - `PredictionGamma`
    - `MinPredictionQuality`
    - `MaxPredictionCost`
    - `PredictionValidityTime`
  - Updates HELLO processing to refresh GPS, ETX, and relative-speed state.
  - Updates TC processing to store advertised prediction metrics in the topology set.
  - Adds prediction-aware MPR tie-breaking.
  - Adds a prediction-cost route computation path.
- `polsr_protocol_experiment.cc`
  - Compares native ns-3 OLSR with the local P-OLSR protocol copy.
- `polsr_header_smoke_test.cc`
  - Verifies HELLO and TC P-OLSR extension serialization.

## Prediction model

The current implementation follows the P-OLSR/ETX structure described in the
local papers:

For every received HELLO with GPS and link-quality information:

1. Cache the neighbor longitude, latitude, and altitude.
2. Smooth locally received HELLO probes into `phi`.
3. Read the neighbor-advertised receiving ratio for this node into `rho`.
4. Estimate and smooth relative speed from adjacent HELLO distance samples.
5. Compute per-link cost as speed-weighted ETX:

```text
cost = exp(PredictionBeta * relativeSpeed) / max(MinPredictionQuality, phi * rho)
```

The cost is capped by `MaxPredictionCost`. Lower cost is better. In ns-3
mobility scenarios, `MobilityModel` local coordinates are mapped into the
longitude/latitude/altitude fields unless a scenario supplies a real GPS
conversion.

## Verification commands

Run from the ns-3 root directory:

```bash
./ns3 configure
./ns3 build scratch/P-OLSR/polsr_header_smoke_test
./ns3 run scratch/P-OLSR/polsr_header_smoke_test
./ns3 build scratch/P-OLSR/polsr_protocol_experiment
./ns3 run "scratch/P-OLSR/polsr_protocol_experiment --scenario=field --protocol=P-OLSR --outputDir=scratch/P-OLSR/protocol-results"
```

For a complete OLSR/P-OLSR comparison:

```bash
bash scratch/P-OLSR/run_protocol_experiments.sh
```

## Known limitations

- This is a protocol-level implementation, not the original OLSRd/P-OLSRd code.
- The HELLO packet fields match the paper intent, but local ns-3 coordinates are
  used as coordinate values unless a real GPS coordinate conversion is added.
- TC prediction currently advertises the local estimate for links to MPR
  selectors. Links learned only through remote topology messages use the
  advertised metric when present and default to cost `1.0` otherwise.
- The route table still stores hop count in the existing `distance` field;
  the prediction cost is used internally for path choice.
