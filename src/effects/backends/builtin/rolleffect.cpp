#include "effects/backends/builtin/rolleffect.h"

#include "effects/backends/effectmanifest.h"
#include "engine/effects/engineeffectparameter.h"
#include "util/math.h"
#include "util/sample.h"

constexpr int RollGroupState::kMaxRollSeconds;

namespace {

// Length steps in beats: 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8.
constexpr int kLengthSteps = 8;
constexpr int kOneBeatStep = 4;

// Used when the track has no detected tempo (120 BPM).
constexpr double kFallbackBeatSeconds = 0.5;

// Fade at the loop edges to avoid clicks.
constexpr double kEdgeFadeSeconds = 0.002;

double stepToBeats(int step) {
    return std::pow(2.0, step - kOneBeatStep);
}

} // anonymous namespace

// static
QString RollEffect::getId() {
    return "org.mixxx.effects.roll";
}

// static
EffectManifestPointer RollEffect::getManifest() {
    EffectManifestPointer pManifest(new EffectManifest());

    // Let the engine crossfade between dry and wet when the effect is
    // switched on or off.
    pManifest->setAddDryToWet(false);
    pManifest->setEffectRampsFromDry(false);

    pManifest->setId(getId());
    pManifest->setName(QObject::tr("Roll"));
    pManifest->setShortName(QObject::tr("Roll"));
    pManifest->setAuthor("The Mixxx Team");
    pManifest->setVersion("1.0");
    pManifest->setDescription(QObject::tr(
            "Records a slice of audio when switched on and repeats it until "
            "switched off. The track keeps playing underneath."));

    EffectManifestParameterPointer length = pManifest->addParameter();
    length->setId("length");
    length->setName(QObject::tr("Length"));
    length->setShortName(QObject::tr("Length"));
    length->setDescription(QObject::tr(
            "Roll length in steps: 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8 beats\n"
            "Uses 120 BPM if no tempo is detected"));
    length->setValueScaler(EffectManifestParameter::ValueScaler::Integral);
    length->setUnitsHint(EffectManifestParameter::UnitsHint::Unknown);
    length->setRange(0, kOneBeatStep, kLengthSteps - 1);

    return pManifest;
}

void RollEffect::loadEngineEffectParameters(
        const QMap<QString, EngineEffectParameterPointer>& parameters) {
    m_pLengthParameter = parameters.value("length");
}

void RollEffect::processChannel(
        RollGroupState* pState,
        const CSAMPLE* pInput,
        CSAMPLE* pOutput,
        const mixxx::EngineParameters& engineParameters,
        const EffectEnableState enableState,
        const GroupFeatureState& groupFeatures) {
    if (enableState == EffectEnableState::Enabling) {
        pState->clear();
    }

    const int step = math_clamp(
            static_cast<int>(std::lround(m_pLengthParameter->value())),
            0,
            kLengthSteps - 1);
    const double beatSeconds = groupFeatures.beat_length.has_value()
            ? groupFeatures.beat_length->seconds
            : kFallbackBeatSeconds;

    const int channels = engineParameters.channelCount();
    const SINT maxFrames = pState->roll_buf.size() / channels;
    const SINT rollFrames = math_clamp(
            static_cast<SINT>(stepToBeats(step) * beatSeconds *
                    engineParameters.sampleRate()),
            static_cast<SINT>(1),
            maxFrames);

    // A longer roll than what was recorded cannot continue the old one,
    // because the audio after it was never captured. Start a new roll.
    if (pState->looping && rollFrames > pState->recorded_frames) {
        pState->clear();
    }

    const SINT fadeFrames = std::max(static_cast<SINT>(1),
            std::min(static_cast<SINT>(kEdgeFadeSeconds * engineParameters.sampleRate()),
                    rollFrames / 4));

    for (SINT i = 0; i < engineParameters.samplesPerBuffer(); i += channels) {
        if (!pState->looping) {
            // First pass: record and play the input. Fade the output out at
            // the end of the slice so the jump into the loop does not click.
            const SINT writeIndex = pState->recorded_frames * channels;
            const CSAMPLE_GAIN gain = std::min(1.0f,
                    static_cast<CSAMPLE_GAIN>(rollFrames - 1 - pState->recorded_frames) /
                            fadeFrames);
            for (int c = 0; c < channels; ++c) {
                pState->roll_buf[writeIndex + c] = pInput[i + c];
                pOutput[i + c] = pInput[i + c] * gain;
            }
            ++pState->recorded_frames;
            if (pState->recorded_frames >= rollFrames) {
                pState->looping = true;
                // The first pass already played the slice once to its end,
                // so the loop starts with the fade-in at frame 0.
                pState->play_frame = 0;
            }
            continue;
        }

        if (pState->play_frame >= rollFrames) {
            // Length got shorter, or the loop reached its end.
            pState->play_frame = 0;
        }

        const SINT pos = pState->play_frame;
        const CSAMPLE_GAIN fadeIn = std::min(1.0f,
                static_cast<CSAMPLE_GAIN>(pos) / fadeFrames);
        const CSAMPLE_GAIN fadeOut = std::min(1.0f,
                static_cast<CSAMPLE_GAIN>(rollFrames - 1 - pos) / fadeFrames);
        const CSAMPLE_GAIN gain = std::min(fadeIn, fadeOut);

        const SINT readIndex = pos * channels;
        for (int c = 0; c < channels; ++c) {
            pOutput[i + c] = pState->roll_buf[readIndex + c] * gain;
        }
        ++pState->play_frame;
    }

    if (enableState == EffectEnableState::Disabling) {
        pState->clear();
    }
}
