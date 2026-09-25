# Pressto design notes

## Reference: Charter Oak SCL-1

The Charter Oak SCL-1 is a two-channel, all-discrete, transformer-coupled compressor/limiter. Reviewers
consistently describe it as unusually transparent, able to handle bus and even classical material
"without a loss of harmonic detail". Public information about how it works:

| Aspect | SCL-1 |
| --- | --- |
| Gain element | Discrete, FET-based VCA. No tubes, ICs or optical cells. |
| Detector | A rectifier with a *parabolic average charge curve*. It combines waveform differentiation and integration: fast releases on rhythmic or staccato peaks, which release **to a continually changing average level** set by the storage capacitors. |
| Attack | 100 µs to 5 ms |
| Hold | < 2 ms |
| Release | 20 ms to 2 s, program-dependent ("a function of dynamic range and average program level") |
| Ratio | Continuous, 1:1 to 20:1 (manual suggests 10:1 as a starting point) |
| "Static threshold" | In practice a continuously variable **knee** control. Clockwise gives a harder knee. |
| "Dynamic threshold" | The conventional threshold, and it interacts strongly with the static threshold. |
| Stereo | Stereo (linked) or dual-mono modes |
| Metering | Gain-reduction VU, switchable between channels |
| Distortion | < 1 % THD at 20 dB of gain reduction, +20 dBm out |

Reviewers named two weaknesses: per-channel input/output controls make true-stereo adjustments fiddly, and
the gain-reduction meter doesn't account for makeup gain.

Sources: the SCL-1 operational manual, and reviews in Sound On Sound (Hugh Robjohns, Aug 2010), AudioTechnology,
George Shilling's review on RecordProduction.com, and the Gearspace mastering forum.

## Was Pressto's original architecture suited to clean, transparent gain reduction?

**Partly.** The broad topology was right. The detector was not.

What it already got right, and what we kept:

* **Feed-forward, log-domain gain computer driving a clean digital VCA.** This design gives predictable,
  ratio-accurate gain reduction with no gain-cell nonlinearity. The SCL-1's transparency doesn't come
  from colouration in its FET cell. It comes from the detector's timing behaviour, and a digital VCA
  reproduces that behaviour cleanly. Feedback, opto or vari-mu models would add character, which is the
  opposite of the goal.
* **The soft-knee static curve.** This maps directly onto the SCL-1's static-threshold (knee) and
  dynamic-threshold pair.

What worked against transparency, and what we changed:

| Problem in the original | Effect | Now |
| --- | --- | --- |
| Instantaneous \|x\| per sample, and the attack branch re-triggered on every waveform peak | Gain ripples inside each low-frequency cycle, giving harmonic distortion on bass | Peak stage with a 15 ms hold (longer than one rectified 35 Hz cycle), so steady-state gain doesn't ripple |
| Release always returned toward 0 dB, and was *slowed* at deep reduction by an ad-hoc curve | Pumping on sustained material, sluggish recovery after heavy transients | SCL-1-style **program-dependent release**: the peak stage releases toward a slowly moving average of the gain reduction |
| Each channel detected on its own, with no link | Stereo image shifts whenever one side is louder | **Stereo link** (shared detector, the default) with a dual-mono option, as on the SCL-1 |
| No sidechain filtering | Kick and bass drive the gain, which audibly modulates the whole mix | **SC HPF** (Off to 500 Hz) on the detector only |
| Unsmoothed input and makeup gain | Zipper noise during automation | 20 ms parameter smoothing |
| Makeup defaulted to +6 dB, and there was no bypass | A/B comparisons weren't level-matched | Makeup defaults to 0 dB. A host-mapped, click-free **Bypass** and a **Mix** control were added |
| Meters sampled only the last block's peak | Peaks were dropped between repaints | Lock-free max-hold accumulators that the UI drains at 60 Hz |

### The detector

```
demand   = static_curve(level_dB)                   // soft-knee gain computer, GR in dB (>= 0)

average  : charges toward demand along a parabolic curve (rate grows with the gap, up to 3x),
           discharges slowly (5 x release, 250 ms to 4 s)
floor    = max(demand, Program x average)

peak     : attack toward demand (Attack), hold 15 ms, then release toward floor (Release)
GR       = peak
```

* **Program = 0 %** gives a conventional peak compressor.
* **Program = 100 %** gives full SCL-1 behaviour: after a short transient the gain snaps back quickly,
  but on sustained passages it stays at the program's average reduction and doesn't pump.

### Measured, using `tests/EngineTests.cpp` (48 kHz, -20 dB threshold, 4:1, hard knee, 3 ms / 100 ms, 0 dBFS tone giving about 15 dB GR)

| | Original detector | New detector |
| --- | --- | --- |
| THD, 40 Hz sustained | 1.54 % | < 0.0001 % |
| THD, 100 Hz sustained | 0.62 % | < 0.0001 % |
| THD, 1 kHz sustained | 0.06 % | < 0.0001 % |
| Recovery to < 1 dB GR after a 20 ms burst | n/a | ≈ 330 ms |
| Recovery to < 1 dB GR after a 1.5 s passage | n/a | ≈ 1080 ms |

The last two rows show the program-dependent release at work: transients recover more than three times
faster than sustained material at the same release setting.

### Not modelled, and why

* **Transformers and the discrete FET cell's own colouration.** These are deliberately left out, because
  the goal is clean gain reduction.
* **Lookahead.** This would add latency. The 0.1 ms minimum attack together with the hold already
  controls transients well for bus use. Lookahead could be added later as an option.
