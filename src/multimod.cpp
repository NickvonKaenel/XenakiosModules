#include "plugin.hpp"
#include "helperwidgets.h"

inline float adjustable_triangle(float in, float peakpos)
{
    in = fmod(in,1.0f);
    if (in<peakpos)
        return rescale(in,0.0f,peakpos,0.0f,1.0f);
    return rescale(in,peakpos,1.0f,1.0f,0.0f);
}

const int msnumtables = 8;
const int mstablesize = 1024;

class ModulationShaper
{
public:
    ModulationShaper()
    {
        float randvalues[1024];
        for (int i=0;i<1024;++i)
            randvalues[i]=random::normal()*0.1f;
        for (int i=0;i<mstablesize;++i)
        {
            float norm = 1.0/(mstablesize-1)*i;
            m_tables[0][i] = std::pow(norm,5.0f);
            m_tables[1][i] = std::pow(norm,2.0f);
            m_tables[2][i] = norm;
            m_tables[3][i] = 0.5-0.5*std::sin(3.141592653*(0.5+norm));
            m_tables[4][i] = 1.0f-std::pow(1.0f-norm,5.0f);
            float smoothrand = interpolateLinear(randvalues,norm*32.0f);
            m_tables[5][i] = clamp(norm+smoothrand,0.0,1.0f);
            smoothrand = interpolateLinear(randvalues,32.0f+norm*48.0f);
            m_tables[6][i] = clamp(norm+smoothrand,0.0,1.0f);
            m_tables[7][i] = std::round(norm*7)/7;
        }
        // fill guard point by repeating value
        for (int i=0;i<msnumtables;++i)
            m_tables[i][mstablesize] = m_tables[i][mstablesize-1];
        // fill guard table by repeating table
        for (int i=0;i<mstablesize;++i)
            m_tables[msnumtables][i]=m_tables[msnumtables-1][i];
    }
    float process(float morph, float input)
    {
        float z = morph*(msnumtables-1);
        int xindex0 = morph*(msnumtables-1);
        int xindex1 = xindex0+1;
        int yindex0 = input*(mstablesize-1);
        int yindex1 = yindex0+1;
        float x_a0 = m_tables[xindex0][yindex0];
        float x_a1 = m_tables[xindex0][yindex1];
        float x_b0 = m_tables[xindex1][yindex0];
        float x_b1 = m_tables[xindex1][yindex0];
        float xfrac = (input*mstablesize)-yindex0;
        float x_interp0 = x_a0+(x_a1-x_a0) * xfrac;
        float x_interp1 = x_b0+(x_b1-x_b0) * xfrac;
        float yfrac=z-(int)z;
        return x_interp0+(x_interp1-x_interp0) * yfrac;
        
    }
private:
    
    float m_tables[msnumtables+1][mstablesize+1];

};

ModulationShaper g_shaper;

class XLFO
{
public:
    XLFO() 
    {
        m_offsetSmoother.setAmount(0.99);
    }
    float process(float masterphase)
    {
        float smoothedOffs = m_offsetSmoother.process(m_offset);
        float out = adjustable_triangle((smoothedOffs+masterphase)*m_freqmultip,m_slope);
        /*
        if (m_shape<0.5f)
            out = std::pow(out,rescale(m_shape,0.0f,0.5f,8.0f,1.0f));
        else 
            out = 1.0f-std::pow(1.0f-out,rescale(m_shape,0.5f,1.0f,1.0f,8.0f));
        */
        out = g_shaper.process(m_shape,out);        

        return -1.0+2.0f*out;
    }
    void setFrequencyMultiplier(float r)
    {
        m_freqmultip = r;
    }
    void setOffset(float offs)
    {
        m_offset = offs;
    }
    void setSlope(float s)
    {
        m_slope = s;
    }
    void setShape(float s)
    {
        m_shape = s;
    }
private:
    // double m_phase = 0.0f;
    double m_freqmultip = 1.0f;
    double m_offset = 0.0f;
    float m_slope = 0.5f;
    float m_shape = 0.5f;
    OnePoleFilter m_offsetSmoother;
};

class XMultiMod : public rack::Module
{
public:
    enum PARAMS
    {
        PAR_RATE,
        PAR_NUMOUTPUTS,
        PAR_OFFSET,
        PAR_FREQMULTIP,
        PAR_SLOPE,
        PAR_SHAPE,
        PAR_VALUEOFFSET,
        PAR_SMOOTHING,
        PAR_ATTN_RATE,
        PAR_LAST
    };
    enum INPUTS
    {
        IN_RESET,
        IN_RATE_CV,
        IN_LAST
    };
    enum OUTPUTS
    {
        OUT_MODOUT,
        OUT_LAST
    };
    XMultiMod()
    {
        config(PAR_LAST,IN_LAST,OUT_LAST);
        configParam(PAR_RATE,-8.0f,10.0f,1.0f,"Base rate", " Hz",2,1);
        configParam(PAR_ATTN_RATE,-1.0f,1.0f,0.0f,"Base rate CV");
        configParam(PAR_NUMOUTPUTS,1,16,4.0,"Number of outputs");
        configParam(PAR_OFFSET,0.0f,1.0f,0.0f,"Outputs phase offset");
        configParam(PAR_FREQMULTIP,-1.0f,1.0f,0.0f,"Outputs frequency multiplication");
        configParam(PAR_SLOPE,0.0f,1.0f,0.5f,"Slope");
        configParam(PAR_SHAPE,0.0f,1.0f,0.5f,"Shape");
        configParam(PAR_VALUEOFFSET,-1.0f,1.0f,0.0f,"Value offset");
        configParam(PAR_SMOOTHING,0.0f,1.0f,0.5f,"Smoothing");
    }
    void updateLFORateMultipliers(int numoutputs, float masterMultip)
    {
        for (int i=0;i<numoutputs;++i)
        {
            if (masterMultip>=0.0f)
            {
                int f = 1.0+i*masterMultip;
                m_lfos[i].setFrequencyMultiplier(f);
            } else
            {
                int f = 1.0+i*(1.0f+masterMultip);
                m_lfos[i].setFrequencyMultiplier(1.0f/f);
            }
            
        }
    }
    void process(const ProcessArgs& args) override
    {
        float pitch = params[PAR_RATE].getValue()*12.0f;
        float rate = std::pow(2.0,1.0/12*pitch);
        float offset = params[PAR_OFFSET].getValue();
        float fmult = params[PAR_FREQMULTIP].getValue();
        float slope = params[PAR_SLOPE].getValue();
        float shape = params[PAR_SHAPE].getValue();
        int numoutputs = params[PAR_NUMOUTPUTS].getValue();
        numoutputs = clamp(numoutputs,1,16);
        outputs[OUT_MODOUT].setChannels(numoutputs);
        float offsetpar = params[PAR_VALUEOFFSET].getValue();
        float smoothing = params[PAR_SMOOTHING].getValue();
        if (m_phase == 0.0)
            updateLFORateMultipliers(numoutputs,fmult);
        for (int i=0;i<numoutputs;++i)
        {
            m_lfos[i].setSlope(slope);
            m_lfos[i].setShape(shape);
            m_lfos[i].setOffset(offset*(1.0/numoutputs*i));
            float out = m_lfos[i].process(m_phase);
            if (smoothing>0.5f)
            {
                smoothing-=0.5f;
                smoothing*=2.0f;
                float gain = 1.0f-m_phase;
                out = gain*std::sin(4.0*smoothing*out*3.141592);
            }
            float voffset = rescale(i,0,numoutputs,0.0f,offsetpar);
            out = clamp(out+voffset,-1.0f,1.0f);
            outputs[OUT_MODOUT].setVoltage(5.0f*out,i);
        }
        m_phase+=args.sampleTime*rate;
        if (m_phase>=1.0)
        {
            m_phase-=1.0;
            updateLFORateMultipliers(numoutputs,fmult);
        }
    }
private:
    XLFO m_lfos[16];
    double m_phase = 0.0;
};

class XMultiModWidget : public ModuleWidget
{
public:
    std::shared_ptr<rack::Font> m_font;
    XMultiModWidget(XMultiMod* m)
    {
        setModule(m);
        box.size.x = 170;
        
        RoundBlackKnob* knob = nullptr;
        float yoffs = 60.0f;
        addChild(new KnobInAttnWidget(this,"FREQUENCY",XMultiMod::PAR_RATE,XMultiMod::IN_RATE_CV,XMultiMod::PAR_ATTN_RATE,1,yoffs));
        addChild(new KnobInAttnWidget(this,"PHASE OFFSET",XMultiMod::PAR_OFFSET,-1,-1,82,yoffs));
        yoffs+=45.0f;
        addChild(new KnobInAttnWidget(this,"SLOPE",XMultiMod::PAR_SLOPE,-1,-1,1,yoffs));
        addChild(new KnobInAttnWidget(this,"SHAPE",XMultiMod::PAR_SHAPE,-1,-1,82,yoffs));
        yoffs+=45.0f;
        addChild(new KnobInAttnWidget(this,"FREQ MULTIP",XMultiMod::PAR_FREQMULTIP,-1,-1,1,yoffs));
        addChild(new KnobInAttnWidget(this,"SMOOTHING",XMultiMod::PAR_SMOOTHING,-1,-1,82,yoffs));
        yoffs+=45.0f;
        addChild(new KnobInAttnWidget(this,"NUM OUTS",XMultiMod::PAR_NUMOUTPUTS,-1,-1,1,yoffs,true));
        addChild(new KnobInAttnWidget(this,"OUTPUT OFFSET",XMultiMod::PAR_VALUEOFFSET,-1,-1,82,yoffs));
        yoffs+=45.0f;
        
        PortWithBackGround<PJ301MPort>* port = nullptr;
        addOutput(port = createOutput<PortWithBackGround<PJ301MPort>>(Vec(4, yoffs+15), m, XMultiMod::OUT_MODOUT));
        port->m_text = "MOD OUT";
        
        addInput(port = createInput<PortWithBackGround<PJ301MPort>>(Vec(40, yoffs+15), m, XMultiMod::IN_RESET));
        port->m_text = "RST";
        port->m_is_out = false;

        addChild(new LabelWidget({{1,187},{166,1}}, "Xenakios",10,nvgRGB(255,255,255),LabelWidget::J_RIGHT));
        addChild(new LabelWidget({{1,6},{170,1}}, "MODULATION SWARM",15,nvgRGB(255,255,255),LabelWidget::J_CENTER));
        
    }
    void draw(const DrawArgs &args) override
    {
        nvgSave(args.vg);
        float w = box.size.x;
        float h = box.size.y;
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGBA(0x50, 0x50, 0x50, 0xff));
        nvgRect(args.vg,0.0f,0.0f,w,h);
        nvgFill(args.vg);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
};

Model* modelXMultiMod = createModel<XMultiMod, XMultiModWidget>("XMultiMod");
