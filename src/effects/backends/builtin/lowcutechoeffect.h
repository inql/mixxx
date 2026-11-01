#pragma once

#include <QMap>

#include "effects/backends/effectprocessor.h"
#include "engine/engine.h"
#include "util/class.h"
#include "util/math.h"
#include "util/samplebuffer.h"

/// Second order (12 dB/oct) high-pass filter that runs one sample at a time,
/// so it can sit inside the echo feedback loop.
/// Coefficients follow the RBJ Audio EQ Cookbook.
class LowCutEchoFilter {
  public:
    void setCutoff(double cutoffHz, double sampleRate) {
        constexpr double kQ = 0.7071; // Butterworth, no resonance peak
        const double w0 = 2.0 * M_PI * cutoffHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * kQ);
        const double a0 = 1.0 + alpha;
        m_b0 = ((1.0 + cosW0) / 2.0) / a0;
        m_b1 = (-(1.0 + cosW0)) / a0;
        m_b2 = m_b0;
        m_a1 = (-2.0 * cosW0) / a0;
        m_a2 = (1.0 - alpha) / a0;
    }

    void clear() {
        m_x1 = m_x2 = m_y1 = m_y2 = 0.0;
    }

    CSAMPLE process(CSAMPLE in) {
        const double y = m_b0 * in + m_b1 * m_x1 + m_b2 * m_x2 -
                m_a1 * m_y1 - m_a2 * m_y2;
        m_x2 = m_x1;
        m_x1 = in;
        m_y2 = m_y1;
        m_y1 = y;
        return static_cast<CSAMPLE>(y);
    }

  private:
    double m_b0 = 1.0;
    double m_b1 = 0.0;
    double m_b2 = 0.0;
    double m_a1 = 0.0;
    double m_a2 = 0.0;
    double m_x1 = 0.0;
    double m_x2 = 0.0;
    double m_y1 = 0.0;
    double m_y2 = 0.0;
};

class LowCutEchoGroupState : public EffectState {
  public:
    // Same limit as EchoGroupState: full range of 2 beats down to 40 BPM.
    static constexpr int kMaxDelaySeconds = 3;

    LowCutEchoGroupState(const mixxx::EngineParameters& engineParameters)
            : EffectState(engineParameters) {
        audioParametersChanged(engineParameters);
        clear();
    }
    ~LowCutEchoGroupState() override = default;

    void audioParametersChanged(const mixxx::EngineParameters& engineParameters) {
        delay_buf = mixxx::SampleBuffer(kMaxDelaySeconds *
                engineParameters.sampleRate() *
                engineParameters.channelCount());
        prev_cutoff = -1.0; // force coefficient update for the new sample rate
    };

    void clear() {
        delay_buf.clear();
        prev_send = 0.0f;
        prev_feedback = 0.0f;
        prev_delay_samples = 0;
        write_position = 0;
        ping_pong = 0;
        prev_cutoff = -1.0;
        filterLeft.clear();
        filterRight.clear();
    };

    mixxx::SampleBuffer delay_buf;
    CSAMPLE_GAIN prev_send;
    CSAMPLE_GAIN prev_feedback;
    int prev_delay_samples;
    int write_position;
    int ping_pong;
    double prev_cutoff;
    LowCutEchoFilter filterLeft;
    LowCutEchoFilter filterRight;
};

/// Echo where every repeat passes through a high-pass filter again, so the
/// repeats lose more low end each time they come back. Based on EchoEffect.
class LowCutEchoEffect : public EffectProcessorImpl<LowCutEchoGroupState> {
  public:
    LowCutEchoEffect() = default;
    ~LowCutEchoEffect() override = default;

    static QString getId();
    static EffectManifestPointer getManifest();

    void loadEngineEffectParameters(
            const QMap<QString, EngineEffectParameterPointer>& parameters) override;

    void processChannel(
            LowCutEchoGroupState* pState,
            const CSAMPLE* pInput,
            CSAMPLE* pOutput,
            const mixxx::EngineParameters& engineParameters,
            const EffectEnableState enableState,
            const GroupFeatureState& groupFeatures) override;

  private:
    QString debugString() const {
        return getId();
    }

    EngineEffectParameterPointer m_pDelayParameter;
    EngineEffectParameterPointer m_pSendParameter;
    EngineEffectParameterPointer m_pFeedbackParameter;
    EngineEffectParameterPointer m_pCutoffParameter;
    EngineEffectParameterPointer m_pPingPongParameter;
    EngineEffectParameterPointer m_pQuantizeParameter;
    EngineEffectParameterPointer m_pTripletParameter;

    DISALLOW_COPY_AND_ASSIGN(LowCutEchoEffect);
};
