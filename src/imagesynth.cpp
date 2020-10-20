#include "plugin.hpp"
#include <random>
#include <stb_image.h>
#include <atomic>
#include <functional>
#include <thread> 
#include "wdl/resample.h"
#include <chrono>

extern std::shared_ptr<Font> g_font;

const int g_wtsize = 2048;
const float g_pi = 3.14159265358979;

template <typename T>
inline T triplemax (T a, T b, T c)                           
{ 
    return a < b ? (b < c ? c : b) : (a < c ? c : a); 
}

inline float soft_clip(float x)
{
    if (x<-1.0f)
        return -2.0f/3.0f;
    if (x>1.0f)
        return 2.0f/3.0f;
    return x-(std::pow(x,3.0f)/3.0f);
}

inline float harmonics3(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3);
}

inline float harmonics4(float xin)
{
    return 0.5 * std::sin(xin) + 0.25 * std::sin(xin * 2.0) + 0.1 * std::sin(xin * 3) +
        0.15*std::sin(xin*7);
}

class ImgWaveOscillator
{
public:
    void initialise(std::function<float(float)> f, 
    int tablesize)
    {
        m_tablesize = tablesize;
        m_table.resize(tablesize);
        for (int i=0;i<tablesize;++i)
            m_table[i] = f(rescale(i,0,tablesize-1,-3.141592653,3.141592653));
    }
    void setFrequency(float hz)
    {
        m_phaseincrement = m_tablesize*hz*(1.0/m_sr);
        m_freq = hz;
    }
    float processSample(float)
    {
        /*
        int index = m_phase;
        float sample = m_table[index];
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        */
        int index0 = std::floor(m_phase);
        int index1 = std::floor(m_phase)+1;
        if (index1>=m_tablesize)
            index1 = 0;
        float frac = m_phase-index0;
        float y0 = m_table[index0];
        float y1 = m_table[index1];
        float sample = y0+(y1-y0)*frac;
        m_phase+=m_phaseincrement;
        if (m_phase>=m_tablesize)
            m_phase-=m_tablesize;
        return sample;
    }
    void prepare(int numchans, float sr)
    {
        m_sr = sr;
        setFrequency(m_freq);
    }
    void reset(float initphase)
    {
        m_phase = initphase;
    }
    void setTable(std::vector<float> tb)
    {
        m_tablesize = tb.size();
        m_table = tb;
    }
private:
    int m_tablesize = 0;
    std::vector<float> m_table;
    double m_phase = 0.0;
    float m_sr = 44100.0f;
    float m_phaseincrement = 0.0f;
    float m_freq = 440.0f;
};

class ImgOscillator
{
public:
    float* m_gainCurve = nullptr;
    ImgOscillator()
    {
        for (int i = 0; i < 4; ++i)
            m_pan_coeffs[i] = 0.0f;
        m_osc.initialise([](float x)
            {
                return 0.5 * std::sin(x) + 0.25 * std::sin(x * 2.0) + 0.1 * std::sin(x * 3);

            }, g_wtsize);
    }
    float m_freq = 440.0f;
    void setFrequency(float hz)
    {
        m_osc.setFrequency(hz);
        m_freq = hz;
    }
    void setEnvelopeAmount(float amt)
    {
        a = rescale(amt, 0.0f, 1.0f, 0.9f, 0.9999f);
        b = 1.0 - a;
    }
    void generate(float pix_mid_gain)
    {
        int gain_index = rescale(pix_mid_gain, 0.0f, 1.0f, 0, 255);
        pix_mid_gain = m_gainCurve[gain_index];
        float z = (pix_mid_gain * b) + (m_env_state * a);
        if (z < m_cut_th)
            z = 0.0;
        m_env_state = z;
        if (z > 0.00)
        {
            outSample = z * m_osc.processSample(0.0f);
        }
        else
            outSample = 0.0f;

    }
    float outSample = 0.0f;
    //private:
    ImgWaveOscillator m_osc;
    float m_env_state = 0.0f;
    float m_pan_coeffs[4];
    float m_cut_th = 0.0f;
    float a = 0.998;
    float b = 1.0 - a;
};

class OscillatorBuilder;

class ImgSynth
{
public:
    std::mt19937 m_rng{ 99937 };
    ImgSynth()
    {
        m_pixel_to_gain_table.resize(256);
        m_oscillators.resize(1024);
        m_freq_gain_table.resize(1024);
        startDirtyCountdown();
    }
    stbi_uc* m_img_data = nullptr;
    int m_img_w = 0;
    int m_img_h = 0;
    void setImage(stbi_uc* data, int w, int h)
    {
        m_img_data = data;
        m_img_w = w;
        m_img_h = h;
        float thefundamental = rack::dsp::FREQ_C4 * pow(2.0, 1.0 / 12 * m_fundamental);
        float f = thefundamental;
        std::vector<float> scale = 
            loadScala("/Users/teemu/Documents/Rack/plugins-v1/NYSTHI/res/microtuning/scala_scales/Ancient Greek Archytas Enharmonic.scl",true);
        if (scale.size()==0 && m_frequencyMapping == 3)
            m_frequencyMapping = 0;
        
        for (int i = 0; i < (int)m_oscillators.size(); ++i)
        {
            if (m_frequencyMapping == 0)
            {
                float pitch = rescale(i, 0, h, 102.0, 0.0);
                float frequency = 32.0 * pow(2.0, 1.0 / 12 * pitch);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 1)
            {
                float frequency = rescale(i, 0, h, 7000.0f, 32.0f);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            if (m_frequencyMapping == 2)
            {
                int harmonic = rescale(i, 0, h, 64.0f, 1.0f);
                f = thefundamental*harmonic;
                std::uniform_real_distribution<float> detunedist(-1.0f,1.0f);
                if (f>127.0f)
                    f+=detunedist(m_rng);
                m_oscillators[i].m_osc.setFrequency(f);
            }
            if (m_frequencyMapping == 3)
            {
                float pitch = rescale(i, 0, h, 102.0, 0.0);
                pitch = quantize_to_grid(pitch,scale,m_scala_quan_amount);
                float frequency = 32.0 * pow(2.0, 1.0 / 12 * pitch);
                m_oscillators[i].m_osc.setFrequency(frequency);
            }
            float curve_begin = 1.0f - m_freq_response_curve;
            float curve_end = m_freq_response_curve;
            float resp_gain = rescale(i, 0, h, curve_end, curve_begin);
            m_freq_gain_table[i] = resp_gain;
            
        }

    }
    void render(float outdur, float sr, OscillatorBuilder& oscbuilder);
    

    float percentReady()
    {
        return m_percent_ready;
    }
    std::vector<float> m_renderBuf;
    float m_maxGain = 0.0f;
    double m_elapsedTime = 0.0f;
    std::atomic<bool> m_shouldCancel{ false };
    
    
    
    int m_stepsize = 64;
    
    
    void setFrequencyMapping(int m)
    {
        if (m!=m_frequencyMapping)
        {
            m_frequencyMapping = m;
            startDirtyCountdown();
        }
    }
    void setFrequencyResponseCurve(float x)
    {
        if (x!=m_freq_response_curve)
        {
            m_freq_response_curve = x;
            startDirtyCountdown();
        }
    }
    void setEnvelopeShape(float x)
    {
        if (x!=m_envAmount)
        {
            m_envAmount = x;
            startDirtyCountdown();
        }
    }
    void setWaveFormType(int x)
    {
        if (x!=m_waveFormType)
        {
            m_waveFormType = x;
            startDirtyCountdown();
        }
    }
    int getWaveFormType() { return m_waveFormType; }
    
    void setHarmonicsFundamental(float semitones)
    {
        if (semitones!=m_fundamental)
        {
            m_fundamental = semitones;
            startDirtyCountdown();
        }
    }

    void setPanMode(int x)
    {
        if (x!=m_panMode)
        {
            m_panMode = x;
            startDirtyCountdown();
        }
    }

    void setPixelGainCurve(float x)
    {
        if (x!=m_pixel_to_gain_curve)
        {
            m_pixel_to_gain_curve = x;
            startDirtyCountdown();
        }
    }

    void setOutputChannelsMode(int m)
    {
        if (m!=m_numOutChans)
        {
            m_numOutChans = m;
            startDirtyCountdown();
        }
    }

    int getNumOutputChannels() { return m_numOutChans; }

    void setScalaTuningAmount(float x)
    {
        if (x!=m_scala_quan_amount)
        {
            m_scala_quan_amount = x;
            startDirtyCountdown();
        }
    }

    void startDirtyCountdown()
    {
        m_isDirty = true;
        m_lastSetDirty = std::chrono::steady_clock::now();
    }
    float getDirtyElapsedTime()
    {
        if (m_isDirty==false)
            return 0.0f;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSetDirty).count();
        return elapsed/1000.0f;
    }
    // keep false while resizing the buffer, the playback code
    // checks that to skip rendering samples
    std::atomic<bool> m_BufferReady{false};
private:
    std::chrono::steady_clock::time_point m_lastSetDirty;
    bool m_isDirty = false;
    int m_frequencyMapping = 0;
    std::vector<ImgOscillator> m_oscillators;
    std::vector<float> m_freq_gain_table;
    std::vector<float> m_pixel_to_gain_table;
    std::atomic<float> m_percent_ready{ 0.0 };
    float m_freq_response_curve = 0.5f;
    float m_envAmount = 0.95f;
    int m_waveFormType = 0;
    int m_currentPreset = 0;
    float m_fundamental = -24.0f; // semitones below middle C!
    int m_panMode = 0;
    int m_numOutChans = 2;
    float m_scala_quan_amount = 0.99f;
    float m_pixel_to_gain_curve = 1.0f;
};

class OscillatorBuilder
{
public:
    OscillatorBuilder(int numharmonics)
    {
        m_table.resize(m_tablesize);
        m_harmonics.resize(numharmonics);
        m_harmonics[0] = 1.0f;
        m_harmonics[1] = 0.5f;
        m_harmonics[2] = 0.25f;
        m_harmonics[4] = 0.125f;
        m_harmonics[13] = 0.5f;
        m_osc.prepare(1,m_samplerate);
        updateOscillator();
    }
    void updateOscillator()
    {
        for (int i=0;i<m_tablesize;++i)
        {
            float sum = 0.0f;
            for (int j=0;j<m_harmonics.size();++j)
            {
                sum+=m_harmonics[j]*std::sin(2*3.141592653/m_tablesize*i*(j+1));
            }
            m_table[i]=sum;
        }
        auto it = std::max_element(m_table.begin(),m_table.end());
        float normscaler = 1.0f / *it;
        for (int i=0;i<m_tablesize;++i)
            m_table[i]*=normscaler;
        m_generating = true;
        m_osc.setTable(m_table);
        m_generating = false;
    }
    float process()
    {
        if (m_generating)
            return 0.0f;
        return m_osc.processSample(0.0f);
    }
    void setFrequency(float hz)
    {
        m_osc.setFrequency(hz);
    }
    float getHarmonic(int index)
    {
        if (index>=0 && index<m_harmonics.size())
            return m_harmonics[index];
        return 0.0f;
    }
    void setHarmonic(int index, float v)
    {
        if (index>=0 && index<m_harmonics.size())
        {
            m_harmonics[index] = v;
            m_dirty = true;
        }
    }
    int getNumHarmonics()
    {
        return m_harmonics.size();
    }
    std::vector<float> getTable()
    {
        return m_table;
    }
    std::vector<float> getTableForFrequency(int size, float hz, float sr)
    {
        std::vector<float> result(size);
        for (int i=0;i<size;++i)
        {
            float sum = 0.0f;
            for (int j=0;j<m_harmonics.size();++j)
            {
                float checkfreq = hz*(j+1);
                if (checkfreq<sr/2.0 && m_harmonics[j]>0.0)
                    sum+=m_harmonics[j]*std::sin(2*3.141592653/m_tablesize*i*(j+1));
            }
            result[i]=sum;
        }
        auto it = std::max_element(result.begin(),result.end());
        float normscaler = 0.0f;
        if (*it>0.0)
            normscaler = 1.0f / *it;
        for (int i=0;i<size;++i)
            result[i]*=normscaler;
        return result;
    }
    bool m_dirty = true;
private:
    std::vector<float> m_harmonics;
    std::vector<float> m_table;
    ImgWaveOscillator m_osc;
    int m_tablesize = g_wtsize;
    float m_samplerate = 44100;
    std::atomic<bool> m_generating{false};
};

void  ImgSynth::render(float outdur, float sr, OscillatorBuilder& oscBuilder)
    {
        m_isDirty = false;
        m_shouldCancel = false;
        m_elapsedTime = 0.0;
        std::uniform_real_distribution<float> dist(0.0, 3.141);
        //double t0 = juce::Time::getMillisecondCounterHiRes();
        const float cut_th = rack::dsp::dbToAmplitude(-72.0f);
        m_maxGain = 0.0f;
        m_percent_ready = 0.0f;
        m_BufferReady = false;
        m_renderBuf.resize(m_numOutChans* ((1.0 + outdur) * sr));
        m_BufferReady = true;
        for (int i = 0; i < 256; ++i)
        {
            m_pixel_to_gain_table[i] = std::pow(1.0 / 256 * i,m_pixel_to_gain_curve);
        }
        
        std::uniform_real_distribution<float> pandist(0.0, 3.141592653 / 2.0f);
        for (int i = 0; i < (int)m_oscillators.size(); ++i)
        {
            m_oscillators[i].m_osc.prepare(1,sr);
            m_oscillators[i].m_osc.reset(dist(m_rng));
            m_oscillators[i].m_env_state = 0.0f;
            m_oscillators[i].m_cut_th = cut_th;
            m_oscillators[i].setEnvelopeAmount(m_envAmount);
            m_oscillators[i].m_gainCurve = m_pixel_to_gain_table.data();
            if (m_waveFormType == 0)
                m_oscillators[i].m_osc.initialise([](float xin){ return std::sin(xin); },g_wtsize);
            else if (m_waveFormType == 1)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                { return harmonics3(xin);},g_wtsize);
            else if (m_waveFormType == 2)
                m_oscillators[i].m_osc.initialise([](float xin)
                                                  { return harmonics4(xin);},g_wtsize);
            else if (m_waveFormType == 3)
            {
                if (oscBuilder.m_dirty)
                {
                    float oschz = m_oscillators[i].m_freq;
                    m_oscillators[i].m_osc.setTable(oscBuilder.getTableForFrequency(g_wtsize,oschz,sr));
                }
                
            }
            if (m_panMode == 0)
            {
                if (m_numOutChans == 2)
                {
                    float panpos = pandist(m_rng);
                    m_oscillators[i].m_pan_coeffs[0] = std::cos(panpos);
                    m_oscillators[i].m_pan_coeffs[1] = std::sin(panpos);
                }
                else if (m_numOutChans == 4)
                {
                    float angle = pandist(m_rng) * 2.0f; // position along circle
                    float panposx = rescale(std::cos(angle), -1.0f, 1.0, 0.0f, 3.141592653);
                    float panposy = rescale(std::sin(angle), -1.0f, 1.0, 0.0f, 3.141592653);
                    m_oscillators[i].m_pan_coeffs[0] = std::cos(panposx);
                    m_oscillators[i].m_pan_coeffs[1] = std::sin(panposx);
                    m_oscillators[i].m_pan_coeffs[2] = std::cos(panposy);
                    m_oscillators[i].m_pan_coeffs[3] = std::sin(panposy);
                }
            }
            if (m_panMode == 1)
            {
                int outspeaker = i % m_numOutChans;
                for (int j = 0; j < m_numOutChans; ++j)
                {
                    if (j == outspeaker)
                        m_oscillators[i].m_pan_coeffs[j] = 1.0f;
                    else m_oscillators[i].m_pan_coeffs[j] = 0.0f;
                }

            }
            if (m_panMode == 2)
            {
                m_oscillators[i].m_pan_coeffs[0] = 0.71f;
                m_oscillators[i].m_pan_coeffs[1] = 0.71f;
                m_oscillators[i].m_pan_coeffs[2] = 0.71f;
                m_oscillators[i].m_pan_coeffs[3] = 0.71f;
            }
        }
        int imgw = m_img_w;
        int imgh = m_img_h;
        int outdursamples = sr * outdur;
        //m_renderBuf.clear();
        for (int x = 0; x < outdursamples; x += m_stepsize)
        {
            if (m_shouldCancel)
                break;
            m_percent_ready = 1.0 / outdursamples * x;
            for (int i = 0; i < m_stepsize; ++i)
            {
                for (int chan=0;chan<m_numOutChans;++chan)
                    m_renderBuf[(x+i)*m_numOutChans+chan]=0.0f;    
            }
            for (int y = 0; y < imgh; ++y)
            {
                int xcor = rescale(x, 0, outdursamples, 0, imgw);
                if (xcor>=imgw)
                    xcor = imgw-1;
                if (xcor<0)
                    xcor = 0;
                const stbi_uc *p = m_img_data + (4 * (y * imgw + xcor));
                unsigned char r = p[0];
                unsigned char g = p[1];
                unsigned char b = p[2];
                //unsigned char a = p[3];
                float pix_mid_gain = (float)triplemax(r,g,b)/255.0f;
                
                for (int i = 0; i < m_stepsize; ++i)
                {
                    m_oscillators[y].generate(pix_mid_gain);
                    float sample = m_oscillators[y].outSample;
                    if (fabs(sample) > 0.0f)
                    {
                        float resp_gain = m_freq_gain_table[y];
                        for (int chan = 0; chan < m_numOutChans; ++chan)
                        {
                            int outbufindex = (x + i)*m_numOutChans+chan;
                            float previous = m_renderBuf[outbufindex];
                            previous += sample * 0.1f * resp_gain * m_oscillators[y].m_pan_coeffs[chan];
                            m_renderBuf[outbufindex] = previous;
                        }
                    }
                }

            }

        }
        if (!m_shouldCancel)
        {
            //m_renderBuf.applyGainRamp(outdursamples - 512, 512 + m_stepsize, 1.0f, 0.0f);
            auto it = std::max_element(m_renderBuf.begin(),m_renderBuf.end());
            m_maxGain = *it; 
            //m_elapsedTime = juce::Time::getMillisecondCounterHiRes() - t0;
        }
        
        m_percent_ready = 1.0;
    }

class XImageSynth : public rack::Module
{
public:
    enum Inputs
    {
        IN_PITCH_CV,
        IN_RESET,
        IN_LOOPSTART_CV,
        IN_LOOPLEN_CV,
        LAST_INPUT
    };
    enum Outputs
    {
        OUT_AUDIO,
        OUT_LOOP_SWITCH,
        OUT_LOOP_PHASE,
        LAST_OUTPUT
    };
    enum Parameters
    {
        PAR_RELOAD_IMAGE,
        PAR_DURATION,
        PAR_PITCH,
        PAR_FREQMAPPING,
        PAR_WAVEFORMTYPE,
        PAR_PRESET_IMAGE,
        PAR_LOOP_START,
        PAR_LOOP_LEN,
        PAR_FREQUENCY_BALANCE,
        PAR_HARMONICS_FUNDAMENTAL,
        PAR_PAN_MODE,
        PAR_NUMOUTCHANS,
        PAR_DESIGNER_ACTIVE,
        PAR_DESIGNER_VOLUME,
        PAR_ENVELOPE_SHAPE,
        PAR_SCALA_TUNING_AMOUNT,
        PAR_LAST
    };
    int m_comp = 0;
    std::list<std::string> presetImages;
    std::vector<stbi_uc> m_backupdata; 
    dsp::BooleanTrigger reloadTrigger;
    std::atomic<bool> m_renderingImage;
    float loopstart = 0.0f;
    float looplen = 1.0f;
    
    OscillatorBuilder m_oscBuilder{32};
    XImageSynth()
    {
        //m_src.SetMode(false,0,true,64,32);
        m_renderingImage = false;
        presetImages = rack::system::getEntries(asset::plugin(pluginInstance, "res/image_synth_images"));
        config(PAR_LAST,LAST_INPUT,LAST_OUTPUT,0);
        configParam(PAR_RELOAD_IMAGE,0,1,1,"Reload image");
        configParam(PAR_DURATION,0.5,60,5.0,"Image duration");
        configParam(PAR_PITCH,-24,24,0.0,"Playback pitch");
        configParam(PAR_FREQMAPPING,0,3,0.0,"Frequency mapping type");
        configParam(PAR_WAVEFORMTYPE,0,3,0.0,"Oscillator type");
        configParam(PAR_PRESET_IMAGE,0,presetImages.size()-1,0.0,"Preset image");
        configParam(PAR_LOOP_START,0.0,0.95,0.0,"Loop start");
        configParam(PAR_LOOP_LEN,0.01,1.00,1.0,"Loop length");
        configParam(PAR_FREQUENCY_BALANCE,0.00,1.00,0.25,"Frequency balance");
        configParam(PAR_HARMONICS_FUNDAMENTAL,-72.0,0.00,-24.00,"Harmonics fundamental");
        configParam(PAR_PAN_MODE,0.0,2.0,0.00,"Frequency panning mode");
        configParam(PAR_NUMOUTCHANS,0.0,4.0,0.00,"Output channels configuration");
        configParam(PAR_DESIGNER_ACTIVE,0,1,0,"Edit oscillator waveform");
        configParam(PAR_DESIGNER_VOLUME,-24.0,3.0,-12.0,"Oscillator editor volume");
        configParam(PAR_ENVELOPE_SHAPE,0.0,1.0,0.95,"Envelope shape");
        configParam(PAR_SCALA_TUNING_AMOUNT,0.0,1.0,0.99,"Scala tuning amount");
        m_syn.setOutputChannelsMode(2);
        //reloadImage();
    }
    void onAdd() override
    {
        reloadImage();
    }
    int renderCount = 0;
    int m_currentPresetImage = 0;
    void reloadImage()
    {
        ++renderCount;
        if (m_renderingImage==true)
            return;
        auto task=[this]
        {
        
        int iw, ih, comp = 0;
        m_img_data = nullptr;
        int imagetoload = params[PAR_PRESET_IMAGE].getValue();
        auto it = presetImages.begin();
        std::advance(it,imagetoload);
        std::string filename = *it;

        auto tempdata = stbi_load(filename.c_str(),&iw,&ih,&comp,4);

        m_playpos = 0.0f;
        //m_bufferplaypos = 0;
        
        int outconf = params[PAR_NUMOUTCHANS].getValue();
        int numoutchans[5]={2,2,4,8,16};
        m_syn.setOutputChannelsMode(numoutchans[outconf]);
        m_img_data = tempdata;
        m_img_data_dirty = true;
        m_syn.setPanMode(params[PAR_PAN_MODE].getValue());
        m_syn.setFrequencyMapping(params[PAR_FREQMAPPING].getValue());
        m_syn.setFrequencyResponseCurve(params[PAR_FREQUENCY_BALANCE].getValue());
        m_syn.setHarmonicsFundamental(params[PAR_HARMONICS_FUNDAMENTAL].getValue());
        int wtype = params[PAR_WAVEFORMTYPE].getValue();
        if (m_syn.getWaveFormType()!=3 && wtype == 3)
            m_oscBuilder.m_dirty = true;
        m_syn.setWaveFormType(wtype);
        m_syn.setEnvelopeShape(params[PAR_ENVELOPE_SHAPE].getValue());
        m_syn.setImage(m_img_data ,iw,ih);
        m_out_dur = params[PAR_DURATION].getValue();
        m_syn.render(m_out_dur,44100,m_oscBuilder);
        m_oscBuilder.m_dirty = false;
        m_renderingImage = false;
        };
        m_renderingImage = true;
        std::thread th(task);
        th.detach();
    }
    int m_timerCount = 0;
    float m_checkOutputDur = 0.0f;
    void onTimer()
    {
        ++m_timerCount;
        if (!m_renderingImage)
        {
            m_syn.setFrequencyResponseCurve(params[PAR_FREQUENCY_BALANCE].getValue());
            m_syn.setFrequencyMapping(params[PAR_FREQMAPPING].getValue());
            m_syn.setEnvelopeShape(params[PAR_ENVELOPE_SHAPE].getValue());
            m_syn.setHarmonicsFundamental(params[PAR_HARMONICS_FUNDAMENTAL].getValue());
            m_syn.setPanMode(params[PAR_PAN_MODE].getValue());
            m_syn.setScalaTuningAmount(params[PAR_SCALA_TUNING_AMOUNT].getValue());
            int outconf = params[PAR_NUMOUTCHANS].getValue();
            int numoutchans[5]={2,2,4,8,16};
            m_syn.setOutputChannelsMode(numoutchans[outconf]);
            int wtype = params[PAR_WAVEFORMTYPE].getValue();
            if (m_syn.getWaveFormType()!=3 && wtype == 3)
                m_oscBuilder.m_dirty = true;
            m_syn.setWaveFormType(wtype);
            int imagetoload = params[PAR_PRESET_IMAGE].getValue();
            if (imagetoload!=m_currentPresetImage)
            {
                m_syn.startDirtyCountdown();
                m_currentPresetImage = imagetoload;
            }
            if (m_checkOutputDur!=params[PAR_DURATION].getValue())
            {
                m_checkOutputDur = params[PAR_DURATION].getValue();
                m_syn.startDirtyCountdown();
            }
            if (m_syn.getDirtyElapsedTime()>0.5)
            {
                reloadImage();
            }
        }
        
    }
    void process(const ProcessArgs& args) override
    {
        int ochans = m_syn.getNumOutputChannels();
        outputs[OUT_AUDIO].setChannels(ochans);
        if (m_syn.m_BufferReady==false)
        {
            outputs[OUT_AUDIO].setVoltage(0.0,0);
            outputs[OUT_AUDIO].setVoltage(0.0,1);
            outputs[OUT_AUDIO].setVoltage(0.0,2);
            outputs[OUT_AUDIO].setVoltage(0.0,3);
            return;
        }
        
        float pitch = params[PAR_PITCH].getValue();
        pitch += inputs[IN_PITCH_CV].getVoltage()*12.0f;
        pitch = clamp(pitch,-36.0,36.0);
        m_src.SetRates(44100 ,44100/pow(2.0,1.0/12*pitch));
        if (params[PAR_DESIGNER_ACTIVE].getValue()>0.5)
        {
            float preview_freq = rack::dsp::FREQ_C4 * pow(2.0, 1.0 / 12 * pitch);
            m_oscBuilder.setFrequency(preview_freq);
            float preview_sample = m_oscBuilder.process();
            float preview_volume = params[PAR_DESIGNER_VOLUME].getValue();
            preview_sample *= rack::dsp::dbToAmplitude(preview_volume);
            outputs[OUT_AUDIO].setVoltage(preview_sample,0);
            outputs[OUT_AUDIO].setVoltage(preview_sample,1);
            return;
        }
        int outlensamps = m_out_dur*args.sampleRate;
        loopstart = params[PAR_LOOP_START].getValue();
        loopstart += inputs[IN_LOOPSTART_CV].getVoltage()/5.0f;
        loopstart = clamp(loopstart,0.0f,1.0f);
        int loopstartsamps = outlensamps*loopstart;
        looplen = params[PAR_LOOP_LEN].getValue();
        looplen += inputs[IN_LOOPLEN_CV].getVoltage()/5.0f;
        looplen = clamp(looplen,0.0f,1.0f);
        int looplensamps = outlensamps*looplen;
        if (looplensamps<256) looplensamps = 256;
        int loopendsampls = loopstartsamps+looplensamps;
        if (loopendsampls>=outlensamps)
            loopendsampls = outlensamps-1;
        int xfadelensamples = 128;
        if (m_bufferplaypos<loopstartsamps)
            m_bufferplaypos = loopstartsamps;
        if (rewindTrigger.process(inputs[IN_RESET].getVoltage()))
            m_bufferplaypos = loopstartsamps;
        if (m_bufferplaypos>=m_out_dur*args.sampleRate)
            m_bufferplaypos = loopstartsamps;
        float loop_phase = rescale(m_bufferplaypos,loopstartsamps,loopendsampls,0.0f,10.0f);
        outputs[OUT_LOOP_PHASE].setVoltage(loop_phase);
        double* rsbuf = nullptr;
        int wanted = m_src.ResamplePrepare(1,ochans,&rsbuf);
        for (int i=0;i<wanted;++i)
        {
            float gain_a = 1.0f;
            float gain_b = 0.0f;
            if (m_bufferplaypos>=loopendsampls-xfadelensamples)
            {
                gain_a = rescale(m_bufferplaypos,loopendsampls-xfadelensamples,loopendsampls,1.0f,0.0f);
                gain_b = 1.0-gain_a;
            }
            int xfadepos = m_bufferplaypos-looplensamps;
            if (xfadepos<0) xfadepos = 0;
            
            for (int j=0;j<ochans;++j)
            {
                rsbuf[i*ochans+j] = gain_a * m_syn.m_renderBuf[m_bufferplaypos*ochans+j];
                
                rsbuf[i*ochans+j] += gain_b * m_syn.m_renderBuf[xfadepos*ochans+j];
            }
            ++m_bufferplaypos;
            if (m_bufferplaypos>=loopendsampls)
            {
                m_bufferplaypos = loopstartsamps;
                loopStartPulse.trigger();
            }
            if (loopStartPulse.process(args.sampleTime))
                outputs[OUT_LOOP_SWITCH].setVoltage(10.0f);
            else
                outputs[OUT_LOOP_SWITCH].setVoltage(0.0f);
        }
        double samples_out[16];
        memset(&samples_out,0,sizeof(double)*16);
        m_src.ResampleOut(samples_out,wanted,1,ochans);
        for (int i=0;i<ochans;++i)
        {
            float outsample = samples_out[i];
            //outsample = soft_clip(outsample);
            outputs[OUT_AUDIO].setVoltage(outsample*5.0,i);
        }
        m_playpos = m_bufferplaypos / args.sampleRate;
        
    }
    float m_out_dur = 10.0f;

    float m_playpos = 0.0f;
    int m_bufferplaypos = 0;
    stbi_uc* m_img_data = nullptr;
    bool m_img_data_dirty = false;
    ImgSynth m_syn;
    WDL_Resampler m_src;
    rack::dsp::SchmittTrigger rewindTrigger;
    rack::dsp::PulseGenerator loopStartPulse;
};
/*
class MySmallKnob : public RoundSmallBlackKnob
{
public:
    XImageSynth* m_syn = nullptr;
    MySmallKnob() : RoundSmallBlackKnob()
    {
        
    }
    void onDragEnd(const event::DragEnd& e) override
    {
        RoundSmallBlackKnob::onDragEnd(e);
        if (m_syn)
        {
            m_syn->reloadImage();
            mLastValue = this->paramQuantity->getValue();
        }
    }
    float mLastValue = 0.0f;
};
*/
class OscDesignerWidget : public TransparentWidget
{
public: 
    XImageSynth* m_syn = nullptr;
    OscDesignerWidget(XImageSynth* s) : m_syn(s)
    {

    }
    void onButton(const event::Button& e) override
    {
        if (e.action == GLFW_RELEASE)
            return;
        float w = box.size.x/m_syn->m_oscBuilder.getNumHarmonics();
        int index = e.pos.x/w;
        float v = rescale(e.pos.y,0.0,300.0,1.0,0.0);
        v = clamp(v,0.0,1.0);
        m_syn->m_oscBuilder.setHarmonic(index,v);
        m_syn->m_oscBuilder.updateOscillator();
    }
    void draw(const DrawArgs &args) override
    {
        if (!m_syn)
            return;
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgFillColor(args.vg, nvgRGB(0,0,0));
        nvgRect(args.vg,0,0,box.size.x,box.size.y);
        nvgFill(args.vg);
        int numharms = m_syn->m_oscBuilder.getNumHarmonics();
        float w = box.size.x / numharms - 2.0f;
        if (w<2.0f)
            w = 2.0f;
        nvgFillColor(args.vg, nvgRGB(0,255,0));
        for (int i=0;i<numharms;++i)
        {
            float v = m_syn->m_oscBuilder.getHarmonic(i);
            if (v>0.0f)
            {
                float xcor = rescale(i,0,numharms-1,0,box.size.x);
                float ycor = v*box.size.y;
                nvgBeginPath(args.vg);
                nvgRect(args.vg,xcor,box.size.y-ycor,w,ycor);
                nvgFill(args.vg);
            }
            
        }
        nvgRestore(args.vg);
    }
};

class XImageSynthWidget : public ModuleWidget
{
public:
    OscDesignerWidget* m_osc_design_widget = nullptr;
    XImageSynth* m_synth = nullptr;
    XImageSynthWidget(XImageSynth* m)
    {
        setModule(m);
        m_synth = m;
        box.size.x = 600.0f;
        if (!g_font)
        	g_font = APP->window->loadFont(asset::plugin(pluginInstance, "res/sudo/Sudo.ttf"));
        
        if (m)
        {
            m_osc_design_widget = new OscDesignerWidget(m);
            m_osc_design_widget->box.pos.x = 0.0;
            m_osc_design_widget->box.pos.y = 0.0;
            m_osc_design_widget->box.size.x = box.size.x;
            m_osc_design_widget->box.size.y = 300.0f;
            addChild(m_osc_design_widget);
        }
        RoundSmallBlackKnob* knob = nullptr;
        addOutput(createOutputCentered<PJ301MPort>(Vec(30, 330), m, XImageSynth::OUT_AUDIO));
        addInput(createInputCentered<PJ301MPort>(Vec(120, 360), m, XImageSynth::IN_PITCH_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(30, 360), m, XImageSynth::IN_RESET));
        addParam(createParamCentered<LEDBezel>(Vec(60.00, 330), m, XImageSynth::PAR_RELOAD_IMAGE));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(90.00, 330), m, XImageSynth::PAR_DURATION));
        //slowknob->m_syn = m;
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(120.00, 330), m, XImageSynth::PAR_PITCH));
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(150.00, 330), m, XImageSynth::PAR_FREQMAPPING));
        knob->snap = true;
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(150.00, 360), m, XImageSynth::PAR_WAVEFORMTYPE));
        knob->snap = true;
        
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(180.00, 330), m, XImageSynth::PAR_PRESET_IMAGE));
        knob->snap = true;
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 330), m, XImageSynth::PAR_LOOP_START));
        addOutput(createOutputCentered<PJ301MPort>(Vec(240, 330), m, XImageSynth::OUT_LOOP_SWITCH));
        addOutput(createOutputCentered<PJ301MPort>(Vec(240, 360), m, XImageSynth::OUT_LOOP_PHASE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(210.00, 360), m, XImageSynth::PAR_LOOP_LEN));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(270.00, 330), m, XImageSynth::PAR_FREQUENCY_BALANCE));
        
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(270.00, 360), m, XImageSynth::PAR_HARMONICS_FUNDAMENTAL));
        
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(300.00, 330), m, XImageSynth::PAR_PAN_MODE));
        knob->snap = true;
        addParam(knob = createParamCentered<RoundSmallBlackKnob>(Vec(300.00, 360), m, XImageSynth::PAR_NUMOUTCHANS));
        knob->snap = true;
        addInput(createInputCentered<PJ301MPort>(Vec(330, 330), m, XImageSynth::IN_LOOPSTART_CV));
        addInput(createInputCentered<PJ301MPort>(Vec(330, 360), m, XImageSynth::IN_LOOPLEN_CV));
        addParam(createParamCentered<CKSS>(Vec(360.00, 330), m, XImageSynth::PAR_DESIGNER_ACTIVE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(360.00, 360), m, XImageSynth::PAR_DESIGNER_VOLUME));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(390.00, 330), m, XImageSynth::PAR_ENVELOPE_SHAPE));
        addParam(createParamCentered<RoundSmallBlackKnob>(Vec(390.00, 360), m, XImageSynth::PAR_SCALA_TUNING_AMOUNT));
        
    }
    
    ~XImageSynthWidget()
    {
        //nvgDeleteImage(m_ctx,m_image);
    }
    int imageCreateCounter = 0;
    bool imgDirty = false;
    void step() override
    {
        if (m_synth==nullptr)
            return;
        
        if (m_synth->params[XImageSynth::PAR_DESIGNER_ACTIVE].getValue()>0.5)
            m_osc_design_widget->show();
        else
        {
            if (m_osc_design_widget->visible)
            {
                m_osc_design_widget->hide();
                m_synth->reloadImage();
            }
            
        }
        float p = m_synth->params[0].getValue();
        if (m_synth->reloadTrigger.process(p>0.0f))
        {
            m_synth->reloadImage();
            
        }
        m_synth->onTimer();
        ModuleWidget::step();
    }
    void draw(const DrawArgs &args) override
    {
        m_ctx = args.vg;
        if (m_synth==nullptr)
            return;
        nvgSave(args.vg);
        if ((m_image == 0 && m_synth->m_img_data!=nullptr))
        {
            m_image = nvgCreateImageRGBA(args.vg,1200,600,NVG_IMAGE_GENERATE_MIPMAPS,m_synth->m_img_data);
            ++imageCreateCounter;
        }
        if (m_synth->m_img_data_dirty)
        {
            nvgUpdateImage(args.vg,m_image,m_synth->m_img_data);
            m_synth->m_img_data_dirty = false;
        }
        int imgw = 0;
        int imgh = 0;
        nvgImageSize(args.vg,m_image,&imgw,&imgh);
        if (imgw>0 && imgh>0)
        {
            auto pnt = nvgImagePattern(args.vg,0,0,600.0f,300.0f,0.0f,m_image,1.0f);
            nvgBeginPath(args.vg);
            nvgRect(args.vg,0,0,600,300);
            nvgFillPaint(args.vg,pnt);
            
            nvgFill(args.vg);
        }
            
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x80, 0x80, 0x80, 0xff));
            nvgRect(args.vg,0.0f,300.0f,box.size.x,box.size.y-300);
            nvgFill(args.vg);

            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            
            float xcor = rescale(m_synth->m_playpos,0.0,m_synth->m_out_dur,0,600);
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopstart = m_synth->loopstart;
            xcor = rescale(loopstart,0.0,1.0,0,600);
            nvgBeginPath(args.vg);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0x00, 0xff));
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            float loopend = m_synth->looplen+loopstart;
            if (loopend>1.0f)
                loopend = 1.0f;

            xcor = rescale(loopend,0.0,1.0,0,600);

            nvgBeginPath(args.vg);
            
            nvgMoveTo(args.vg,xcor,0);
            nvgLineTo(args.vg,xcor,300);
            nvgStroke(args.vg);

            nvgFontSize(args.vg, 15);
            nvgFontFaceId(args.vg, g_font->handle);
            nvgTextLetterSpacing(args.vg, -1);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
            char buf[100];
            float dirtyElapsed = m_synth->m_syn.getDirtyElapsedTime();
            sprintf(buf,"%d %d %d %d %d %f",imgw,imgh,m_image,imageCreateCounter,m_synth->renderCount,
                dirtyElapsed);
            nvgText(args.vg, 3 , 10, buf, NULL);
        
        float progr = m_synth->m_syn.percentReady();
        if (progr<1.0)
        {
            float progw = rescale(progr,0.0,1.0,0.0,box.size.x);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0x00, 0x9f, 0x00, 0xa0));
            nvgRect(args.vg,0.0f,280.0f,progw,20);
            nvgFill(args.vg);
        }
        float dirtyTimer = m_synth->m_syn.getDirtyElapsedTime();
        if (dirtyTimer<=0.5)
        {
            float progw = rescale(dirtyTimer,0.0,0.5,0.0,box.size.x);
            nvgBeginPath(args.vg);
            nvgFillColor(args.vg, nvgRGBA(0xff, 0x00, 0x00, 0xa0));
            nvgRect(args.vg,0.0f,280.0f,progw,20);
            nvgFill(args.vg);
        }
        
        
        //nvgDeleteImage(args.vg,m_image);
        nvgRestore(args.vg);
        ModuleWidget::draw(args);
    }
private:
    NVGcontext* m_ctx = nullptr;
    int m_image = 0;
};

Model* modelXImageSynth = createModel<XImageSynth, XImageSynthWidget>("XImageSynth");
