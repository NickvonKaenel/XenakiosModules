#include "weightedrandom.h"

WeightedRandomModule::WeightedRandomModule()
{
    config(LASTPAR, 9, 8, 0);
	for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        m_outcomes[i]=false;
        float defval = 0.0f;
        if (i == 0)
            defval = 50.0f;
        configParam(W_0+i, 0.0f, 100.0f, defval, "Weight "+std::to_string(i+1), "", 0, 1.0);
    }
}

void WeightedRandomModule::process(const ProcessArgs& args)
{
    float trigscaled = rescale(inputs[0].getVoltage(), 0.1f, 2.f, 0.f, 1.f);
    if (m_trig.process(trigscaled))
    {
        // This maybe isn't the most efficient way to do this but since it's only run when
        // the clock triggers, maybe good enough for now...
        int result = 0;
        m_in_trig_high = true;
        float accum = 0.0f;
        float scaledvalues[8];
        for (int i=0;i<WR_NUM_OUTPUTS;++i)
        {
            float sv = params[W_0+i].getValue()+rescale(inputs[i+1].getVoltage(),0.0,10.0f,0.0,100.0f);
            sv = clamp(sv,0.0,100.0f);
            accum+=sv;
            scaledvalues[i]=sv;
        }
        if (accum>0.0f) // skip updates if all weights are zero. maybe need to handle this better?
        {
            float scaler = 1.0/accum;
            float z = rack::random::uniform();
            accum = 0.0f;
            for (int i=0;i<WR_NUM_OUTPUTS;++i)
            {
                accum+=scaledvalues[i]*scaler;
                if (accum>=z)
                {
                    result = i;
                    break;
                }
            }
            if (result>=0 && result<WR_NUM_OUTPUTS)
            {
                for (int i=0;i<WR_NUM_OUTPUTS;++i)
                {
                    m_outcomes[i] = i == result;
                }
                
            }
            
        }
        
    }
    for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        if (m_outcomes[i])
            outputs[i].setVoltage(inputs[0].getVoltage());    
        else
            outputs[i].setVoltage(0.0f);    
    }
}

WeightedRandomWidget::WeightedRandomWidget(WeightedRandomModule* mod)
{
    //if (!g_font)
    //	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
    setModule(mod);
    box.size.x = 130;
    
    addInput(createInput<PJ301MPort>(Vec(5, 20), module, 0));
    
    for (int i=0;i<WR_NUM_OUTPUTS;++i)
    {
        addInput(createInput<PJ301MPort>(Vec(5, 50+i*40), module, i+1));
        addParam(createParam<RoundLargeBlackKnob>(Vec(38, 40+i*40), module, i));
        addOutput(createOutput<PJ301MPort>(Vec(85, 50+i*40), module, i));
        
    }
}

void WeightedRandomWidget::draw(const DrawArgs &args)
{
    nvgSave(args.vg);
	float w = box.size.x;
    float h = box.size.y;
    nvgBeginPath(args.vg);
    nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
    nvgRect(args.vg,0.0f,0.0f,w,h);
    nvgFill(args.vg);
    nvgRestore(args.vg);
	ModuleWidget::draw(args);
}