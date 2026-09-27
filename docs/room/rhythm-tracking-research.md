# Musical rhythm rather than bass-only reactivity

The user's priority is a convincing response to pulse, accents and groove,
regardless of whether the implementation uses a neural network. This note is
research, not a claim that a new rhythm tracker is installed.

Room Pulse currently has a bass-onset envelope, not tempo, beat-phase or meter
tracking. Its inherited renderer consumes a latest-512-sample snapshot at effect
frame rate. At 48 kHz and 30 FPS this covers 10.7 ms per 33.3 ms render interval;
intervening attacks can be overwritten. Any improved tracker needs continuous,
timestamped audio processing independent of rendering. That prerequisite matters
for both conventional DSP and learned models.

Candidate sources inspected:

- [BeatNet](https://github.com/mjhydri/BeatNet) combines a learned causal CRNN
  with particle filtering for beats, first beats of measures, tempo and meter.
- [BeatNet+](https://github.com/mjhydri/BeatNet-Plus) extends the approach to
  better handle music with less percussion. It is a candidate to evaluate, not
  a ready-made native OpenRGB effect or a demonstrated improvement on this PC.
- [Oculizer's predictor](https://github.com/LandryBulls/Oculizer/blob/62e864d4d175837d3ca3e826aaa9d2a8b7b64a98/oculizer/scene_predictors/v4/predictor.py)
  uses learned audio embeddings for scene selection. This does not provide the
  precise rhythmic clock required for synchronized light movement.
- [CanYuzbey's tempo estimator](https://github.com/CanYuzbey/music-reactive-lighting/blob/68880191d8c232e92ec7e3a80bbe39941709d749/app/audio/tempo.py)
  estimates BPM, while its separate
  [pulse tracker](https://github.com/CanYuzbey/music-reactive-lighting/blob/68880191d8c232e92ec7e3a80bbe39941709d749/app/lighting/pulse.py)
  uses attack thresholds. It does not expose a beat-phase grid to rendering.

A suitable native design separates continuous audio capture/analysis from GLSL
rendering. It combines a reliable beat clock with real transient accents and
frequency-band intensity; confidence must control how long a prediction is
trusted. A sustained bass note must not become a stream of invented beats.
Models, if used, need verified preprocessing and native inference, without a
separate Python lighting server.

Compare candidates on the user's music plus annotated examples: syncopation,
missing kicks, changes of tempo, half/double-time ambiguity, silence and resumes.
Measure detection accuracy and audio-to-light delay separately. Rendering at
30/60 FPS or a temporary rendering stall must not change the detected beat
sequence. No model was installed or executed during this research.
