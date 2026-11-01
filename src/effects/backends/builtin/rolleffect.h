#pragma once

#include <QMap>

#include "effects/backends/effectprocessor.h"
#include "engine/engine.h"
#include "util/class.h"
#include "util/samplebuffer.h"

class RollGroupState : public EffectState {
  public:
    // Enough for the longest roll (8 beats) down to 60 BPM. Longer rolls at
    // slower tempos are clamped to this length.
    static constexpr int kMaxRollSeconds = 8;

    RollGroupState(const mixxx::EngineParameters& engineParameters)
            : EffectState(engineParameters) {
        audioParametersChanged(engineParameters);
        clear();
    }
    ~RollGroupState() override = default;

    void audioParametersChanged(const mixxx::EngineParameters& engineParameters) {
        roll_buf = mixxx::SampleBuffer(kMaxRollSeconds *
                engineParameters.sampleRate() *
                engineParameters.channelCount());
        clear();
    };

    void clear() {
        roll_buf.clear();
        recorded_frames = 0;
        play_frame = 0;
        looping = false;
    };

    mixxx::SampleBuffer roll_buf;
    // Frames captured since the roll started.
    SINT recorded_frames;
    // Playback position inside the loop, only used while looping.
    SINT play_frame;
    // False while the first pass is still being recorded.
    bool looping;
};

/// Beat roll: captures a beat-length slice of audio when enabled and repeats
/// it until disabled. The input keeps running underneath, so turning the
/// effect off returns to where the track would be anyway (like slip mode).
class RollEffect : public EffectProcessorImpl<RollGroupState> {
  public:
    RollEffect() = default;
    ~RollEffect() override = default;

    static QString getId();
    static EffectManifestPointer getManifest();

    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;

    void processChannel(
            RollGroupState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    QString debugString() const {
        return getId();
    }

    EngineEffectParameterPointer m_pLengthParameter;

    DISALLOW_COPY_AND_ASSIGN(RollEffect);
};
