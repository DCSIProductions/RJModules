#include "RJModules.hpp"
#include "dsp/samplerate.hpp"
#include "dsp/ringbuffer.hpp"
#include "dsp/filter.hpp"

#define HISTORY_SIZE (1<<21)

struct FilterDelay : Module {
    enum ParamIds {
        TIME_PARAM,
        FEEDBACK_PARAM,
        COLOR_PARAM,
        MIX_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        TIME_INPUT,
        FEEDBACK_INPUT,
        COLOR_INPUT,
        MIX_INPUT,
        IN_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        OUT_OUTPUT,
        NUM_OUTPUTS
    };

    DoubleRingBuffer<float, HISTORY_SIZE> historyBuffer;
    DoubleRingBuffer<float, 16> outBuffer;
    SampleRateConverter<1> src;
    float lastWet = 0.0;
    RCFilter lowpassFilter;
    RCFilter highpassFilter;

    FilterDelay() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS) {}

    void step() override;
};


void FilterDelay::step() {
    // Get input to delay block
    float in = inputs[IN_INPUT].value;
    float feedback = clampf(params[FEEDBACK_PARAM].value + inputs[FEEDBACK_INPUT].value / 10.0, 0.0, 0.99);
    float dry = in + lastWet * feedback;

    // Compute delay time in seconds
    float delay = .001 * powf(10.0 / .001, clampf(params[TIME_PARAM].value + inputs[TIME_INPUT].value / 10.0, 0.0, 1.0));
    // Number of delay samples
    float index = delay * engineGetSampleRate();

    // The delay algorithm is taken from Fundamentals,
    // which it seems is written by somebody who didn't know what they're doing,
    // at least according to the old comments.

    // Push dry sample into history buffer
    if (!historyBuffer.full()) {
        historyBuffer.push(dry);
    }

    // How many samples do we need consume to catch up?
    float consume = index - historyBuffer.size();
    if (outBuffer.empty()) {
        double ratio = 1.0;
        if (consume <= -16)
            ratio = 0.5;
        else if (consume >= 16)
            ratio = 2.0;

        int inFrames = mini(historyBuffer.size(), 16);
        int outFrames = outBuffer.capacity();
        src.setRatioSmooth(ratio);
        src.process((const Frame<1>*)historyBuffer.startData(), &inFrames, (Frame<1>*)outBuffer.endData(), &outFrames);
        historyBuffer.startIncr(inFrames);
        outBuffer.endIncr(outFrames);
    }

    float wet = 0.0;
    if (!outBuffer.empty()) {
        wet = outBuffer.shift();
    }

    float color = clampf(params[COLOR_PARAM].value + inputs[COLOR_INPUT].value / 10.0, 0.0, 1.0);
    float lowpassFreq = 4000.0 * powf(10.0, clampf(2.0*color, 0.0, 1.0));
    lowpassFilter.setCutoff(lowpassFreq / engineGetSampleRate());
    lowpassFilter.process(wet);
    wet = lowpassFilter.lowpass();

    // No muddy sub
    float highpassFreq = 80.0;
    highpassFilter.setCutoff(highpassFreq / engineGetSampleRate());
    highpassFilter.process(wet);
    wet = highpassFilter.highpass();

    if (!historyBuffer.full()) {
        historyBuffer.push(wet);
    }

    lastWet = wet;

    float mix = clampf(params[MIX_PARAM].value + inputs[MIX_INPUT].value / 10.0, 0.0, 1.0);
    float out = crossf(in, wet, mix);
    outputs[OUT_OUTPUT].value = out;
}


FilterDelayWidget::FilterDelayWidget() {
    FilterDelay *module = new FilterDelay();
    setModule(module);
    box.size = Vec(15*10, 380);

    {
        SVGPanel *panel = new SVGPanel();
        panel->box.size = box.size;
        panel->setBackground(SVG::load(assetPlugin(plugin, "res/FilterDelay.svg")));
        addChild(panel);
    }

    addChild(createScrew<ScrewSilver>(Vec(15, 0)));
    addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
    addChild(createScrew<ScrewSilver>(Vec(15, 365)));
    addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

    addParam(createParam<RoundBlackKnob>(Vec(97, 60), module, FilterDelay::TIME_PARAM, 0.0, 1.0, 0.5));
    addParam(createParam<RoundBlackKnob>(Vec(97, 120), module, FilterDelay::FEEDBACK_PARAM, 0.0, 1.0, 0.5));
    addParam(createParam<RoundBlackKnob>(Vec(97, 180), module, FilterDelay::COLOR_PARAM, 0.0, 1.0, 0.5));
    addParam(createParam<RoundBlackKnob>(Vec(97, 240), module, FilterDelay::MIX_PARAM, 0.0, 1.0, 0.5));

    addInput(createInput<PJ301MPort>(Vec(22, 65), module, FilterDelay::TIME_INPUT));
    addInput(createInput<PJ301MPort>(Vec(22, 125), module, FilterDelay::FEEDBACK_INPUT));
    addInput(createInput<PJ301MPort>(Vec(22, 185), module, FilterDelay::COLOR_INPUT));
    addInput(createInput<PJ301MPort>(Vec(22, 245), module, FilterDelay::MIX_INPUT));

    addInput(createInput<PJ301MPort>(Vec(22, 305), module, FilterDelay::IN_INPUT));
    addOutput(createOutput<PJ301MPort>(Vec(105, 305), module, FilterDelay::OUT_OUTPUT));
}