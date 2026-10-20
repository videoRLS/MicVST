#include "audio/BuiltInEffects.h"
#include "audio/fx/EffectFactories.h"
#include "ui/BuiltInEffectEditor.h"

namespace
{
    const juce::Identifier stateTag ("MicVSTEffect"), idAttr ("id"), versionAttr ("version"),
                           paramTag ("param"), valueAttr ("value");

    juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

    // Inputs beyond +24 dBFS are no signal but a plug-in out of control (as AudioEngine::isValidAudio).
    bool isValid (const float* const* ch, int numChannels, int numSamples) noexcept
    {
        constexpr float limit = 16.0f;
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i)
                if (! (std::abs (ch[c][i]) <= limit)) return false;
        return true;
    }

    bool isFinite (const float* const* ch, int numChannels, int numSamples) noexcept
    {
        for (int c = 0; c < numChannels; ++c)
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (ch[c][i])) return false;
        return true;
    }
}

//==============================================================================
BuiltInEffect::BuiltInEffect (const Info& i)
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      info (i)
{
}

BuiltInEffect::~BuiltInEffect()
{
    for (auto* p : params)
        p->removeListener (this);
}

const juce::String BuiltInEffect::getName() const { return utf8 (info.name); }

juce::AudioProcessorEditor* BuiltInEffect::createEditor() { return new BuiltInEffectEditor (*this); }

juce::AudioParameterFloat* BuiltInEffect::addFloat (const char* paramId, const char* name, float min, float max, float def,
                                                    Unit unit, float centre, float step)
{
    juce::NormalisableRange<float> range (min, max, step);
    if (centre > min && centre < max)
        range.setSkewForCentre (centre);
    auto attrs = juce::AudioParameterFloatAttributes()
                     .withStringFromValueFunction ([unit] (float v, int) { return builtin::formatValue (v, unit); })
                     .withValueFromStringFunction ([unit] (const juce::String& t) { return builtin::parseValue (t, unit); });
    auto* p = new juce::AudioParameterFloat (juce::ParameterID { paramId, 1 }, utf8 (name), range, def, attrs);
    addParameter (p);
    params.add (p);
    p->addListener (this);
    return p;
}

juce::AudioParameterChoice* BuiltInEffect::addChoice (const char* paramId, const char* name, const juce::StringArray& choices, int def)
{
    auto* p = new juce::AudioParameterChoice (juce::ParameterID { paramId, 1 }, utf8 (name), choices, def);
    addParameter (p);
    params.add (p);
    p->addListener (this);
    return p;
}

juce::AudioParameterBool* BuiltInEffect::addBool (const char* paramId, const char* name, bool def)
{
    auto attrs = juce::AudioParameterBoolAttributes()
                     .withStringFromValueFunction ([] (bool v, int) { return utf8 (v ? "Açık" : "Kapalı"); });
    auto* p = new juce::AudioParameterBool (juce::ParameterID { paramId, 1 }, utf8 (name), def, attrs);
    addParameter (p);
    params.add (p);
    p->addListener (this);
    return p;
}

void BuiltInEffect::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (1, maximumBlockSize);
    prepareEffect();
    resetEffect();
    lastActive = getActiveChannels();
    pendingReset = false;
    prepared = true;
    updateLatency();
}

void BuiltInEffect::reset()
{
    if (prepared)
        resetEffect();
}

void BuiltInEffect::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), info.stereo ? 2 : getActiveChannels());
    if (! prepared || n <= 0 || numCh <= 0)
        return;

    float* ch[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    // A channel that was not wired before carries an old state: start from silence.
    if (const int a = getActiveChannels(); a != lastActive)
    {
        lastActive = a;
        pendingReset = true;
    }

    if (! isValid (ch, numCh, n))
    {
        // passed on untouched (the engine silences it); it never enters the filters
        rejectedInput.fetch_add (1, std::memory_order_relaxed);
        pendingReset = true;
        return;
    }
    if (pendingReset)
    {
        resetEffect();
        pendingReset = false;
    }

    processEffect (ch, numCh, n);

    if (! isFinite (ch, numCh, n))
    {
        for (int c = 0; c < numCh; ++c)
            juce::FloatVectorOperations::clear (ch[c], n);
        resetEffect();
        repairedOutput.fetch_add (1, std::memory_order_relaxed);
    }
}

void BuiltInEffect::updateLatency()
{
    setLatencySamples (juce::jmax (0, computeLatency()));
}

void BuiltInEffect::parameterValueChanged (int, float)
{
    // The plug-in thread reads the parameters itself; here only the latency (message thread).
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
        updateLatency();
}

//==============================================================================
juce::RangedAudioParameter* BuiltInEffect::findParameter (const juce::String& paramId) const
{
    for (auto* p : params)
        if (p->getParameterID() == paramId)
            return p;
    return nullptr;
}

float BuiltInEffect::getParam (const juce::String& paramId) const
{
    if (auto* p = findParameter (paramId))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void BuiltInEffect::setParam (const juce::String& paramId, float value)
{
    if (auto* p = findParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
}

void BuiltInEffect::resetToDefaults()
{
    for (auto* p : params)
        p->setValueNotifyingHost (p->getDefaultValue());
}

std::unique_ptr<juce::XmlElement> BuiltInEffect::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (stateTag.toString());
    xml->setAttribute (idAttr, info.id);
    xml->setAttribute (versionAttr, 1);
    for (auto* p : params)
    {
        auto* e = xml->createNewChildElement (paramTag.toString());
        e->setAttribute (idAttr, p->getParameterID());
        e->setAttribute (valueAttr, (double) p->convertFrom0to1 (p->getValue()));
    }
    return xml;
}

bool BuiltInEffect::applyStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (stateTag.toString()) || xml.getStringAttribute (idAttr) != juce::String (info.id))
        return false;
    for (auto* e : xml.getChildWithTagNameIterator (paramTag.toString()))
        if (auto* p = findParameter (e->getStringAttribute (idAttr)); p != nullptr && e->hasAttribute (valueAttr))
        {
            const double v = e->getDoubleAttribute (valueAttr);
            if (std::isfinite (v))
                p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue ((float) v)));
        }
    return true;
}

void BuiltInEffect::getStateInformation (juce::MemoryBlock& dest)
{
    copyXmlToBinary (*createStateXml(), dest);
}

void BuiltInEffect::setStateInformation (const void* data, int sizeInBytes)
{
    // Unknown or damaged data keeps the current values (the defaults for a new element).
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

//==============================================================================
namespace builtin
{
    const std::vector<BuiltInEffect::Info>& effects()
    {
        static const char* const eq      = "Dahili - Ton ve EQ";
        static const char* const clean   = "Dahili - Temizlik";
        static const char* const colour  = "Dahili - Renk ve ortam";
        static const char* const level   = "Dahili - Seviye ve koruma";
        static const char* const timeCh  = "Dahili - Zaman ve kanal";
        static const char* const meter   = "Dahili - Ölçüm";

        static const std::vector<BuiltInEffect::Info> list {
            { "builtin:lowcut", "Alçak Kesici (Low-Cut)", eq,
              "Seçilen frekansın altını keser: uğultu, masa/stand titreşimi ve \"p\" patlamalarının alt ucu. "
              "Sesin temeli (erkek sesinde ~100-140 Hz) korunur; 100 Hz'in üstü önerilmez.", false, &fx::createLowCut },
            { "builtin:highcut", "Yüksek Kesici (High-Cut)", eq,
              "Seçilen frekansın üstünü keser: cızırtı ve tıslama.", false, &fx::createHighCut },
            { "builtin:body", "Gövde / Sıcaklık", eq,
              "120-250 Hz bölgesini geniş bir eğriyle yükseltir: daha tok, dolgun ses. Fazlası boğuklaştırır.", false, &fx::createBody },
            { "builtin:mud", "Çamur Temizleyici", eq,
              "300-500 Hz bölgesini kısar: boğukluk ve \"kutu\" sesi azalır.", false, &fx::createMud },
            { "builtin:nasal", "Genizlik Azaltıcı", eq,
              "0,8-1,3 kHz bölgesini kısar: genizden gelen, sert orta bölge yumuşar.", false, &fx::createNasal },
            { "builtin:presence", "Netlik (Presence)", eq,
              "3-5 kHz bölgesini yükseltir: anlaşılırlık artar. Fazlası sertleştirir.", false, &fx::createPresence },
            { "builtin:air", "Hava (Air)", eq,
              "Seçilen frekansın üstünü raf eğrisiyle yükseltir: parlaklık ve açıklık.", false, &fx::createAir },
            { "builtin:proximity", "Yakınlık (Proximity)", eq,
              "Mikrofona yakın konuşmanın getirdiği alt bölge tokluğunu ekler (+) ya da fazlasını azaltır (-).", false, &fx::createProximity },
            { "builtin:tilt", "Tilt EQ (Ton Dengesi)", eq,
              "Tek kaydırıcıyla tüm tonu eğer: sola sıcak (bas artar, tiz azalır), sağa parlak.", false, &fx::createTilt },
            { "builtin:parameq", "Parametrik EQ", eq,
              "Dört bant: her birinde tür, frekans, kazanç ve Q. Kazancı 0 dB olan bant sesi değiştirmez.", false, &fx::createParamEq },

            { "builtin:hum", "Uğultu Giderici (50/60 Hz)", clean,
              "Şebeke uğultusunu (50 Hz ve katları; 60 Hz seçilebilir) çok dar çentiklerle giderir.", false, &fx::createHum },
            { "builtin:dcblock", "DC Kayması Giderici", clean,
              "Sinyaldeki sabit kaymayı (DC) 5 Hz'in altında giderir. Ayarı yoktur; duyulur bir şey değiştirmez.", false, &fx::createDcBlock },
            { "builtin:deesser", "De-esser", clean,
              "Sert \"s\", \"ş\" ve \"ç\" seslerini yalnızca oldukları anda, seçilen frekansın üstünde kısar. "
              "Göreli algılamada eşik, tiz bölgenin sesin geneline göre seviyesidir (-15 dB: tizler sesin geneline 15 dB "
              "yaklaşınca kısar; mikrofon seviyesinden bağımsız). En fazla \"Azaltma\" kadar kısar.", false, &fx::createDeEsser },

            { "builtin:saturation", "Sıcak Saturasyon (Tüp / Bant)", colour,
              "Hafif harmonik ekler (4x aşırı örnekleme ile, kenar yumuşatmalı). Seviye otomatik dengelenir.", false, &fx::createSaturation },
            { "builtin:lofi", "Telefon / Radyo / Megafon", colour,
              "Sesi telefon, eski radyo ya da megafon sesine benzetir.", false, &fx::createLoFi },
            { "builtin:reverb", "Oda Yankısı", colour,
              "Basit oda yankısı. Konuşmada düşük karışım (%5-15) önerilir.", true, &fx::createReverb },
            { "builtin:echo", "Gecikme / Eko", colour,
              "Tekrarlayan yankı. Geri besleme tekrar sayısını, ton tekrarların parlaklığını belirler.", true, &fx::createEcho },
            { "builtin:chorus", "Chorus / Stereo Genişletici", colour,
              "Chorus: hafif dalgalanan çift ses. Genişletici: mono sesi stereo genişletir; iki kanal "
              "toplanınca (mono) ses değişmez.", true, &fx::createChorus },

            { "builtin:limiter", "Çıkış Limiteri", level,
              "Hiçbir tepenin tavanı geçmemesini sağlar (gerçek tepe, 3 ms ileri bakışlı, Otomatik Kazanç'taki "
              "sınırlayıcıyla aynı yöntem). Zincirin sonuna koyun.", false, &fx::createLimiter },
            { "builtin:mute", "Sessize Alma (Mute)", level,
              "Sesi tıksız (yumuşak geçişle) susturur. Satırdaki kutuyla da açılıp kapatılır.", false, &fx::createMute },

            { "builtin:syncdelay", "Ses Gecikmesi (Senkron)", timeCh,
              "Sesi verilen süre kadar geciktirir: görüntüyle (kamera, yayın) hizalamak için.", false, &fx::createSyncDelay },
            { "builtin:channel", "Kanal Araçları (Faz / Kanal / Denge)", timeCh,
              "Faz çevirme, sol ya da sağ kanalı iki kanala verme, kanalları değiştirme, mono toplama ve sağ-sol denge.",
              true, &fx::createChannelTool },

            { "builtin:lufs", "LUFS Ölçer", meter,
              "ITU-R BS.1770 / EBU R128 ses yüksekliği: anlık (400 ms), kısa süreli (3 s), tümleşik ve gerçek tepe. "
              "Sesi değiştirmez.", false, &fx::createLufsMeter },
            { "builtin:spectrum", "Spektrum Analizörü", meter,
              "Sesin frekans dağılımını canlı gösterir. Sesi değiştirmez.", false, &fx::createSpectrum },
        };
        return list;
    }

    const BuiltInEffect::Info* find (const juce::String& id)
    {
        for (auto& i : effects())
            if (id == i.id)
                return &i;
        return nullptr;
    }

    bool isEffectId (const juce::String& id) { return find (id) != nullptr; }

    std::unique_ptr<BuiltInEffect> create (const juce::String& id)
    {
        if (auto* i = find (id))
            return i->create (*i);
        return nullptr;
    }

    juce::String formatValue (float v, BuiltInEffect::Unit unit)
    {
        using U = BuiltInEffect::Unit;
        auto num = [] (double x, int decimals)
        {
            if (decimals <= 0) return juce::String (juce::roundToInt (x));
            auto s = juce::String (x, decimals);
            if (s.startsWithChar ('-') && s.removeCharacters ("-0.").isEmpty()) s = s.substring (1);   // never "-0,0"
            return s.replaceCharacter ('.', ',');
        };
        switch (unit)
        {
            case U::hertz:
                if (v >= 1000.0f) return num (v / 1000.0, v < 10000.0f ? 2 : 1) + " kHz";
                return num (v, v < 10.0f ? 2 : 0) + " Hz";
            case U::decibel:
            {
                const double r = std::round (v * 10.0) / 10.0;
                return (r > 0.0 ? "+" : "") + num (std::abs (r) < 0.05 ? 0.0 : r, 1) + " dB";
            }
            case U::dbAmount:     return num (std::round (v * 10.0) / 10.0, 1) + " dB";
            case U::percent:      return "%" + juce::String (juce::roundToInt (v));
            case U::milliseconds: return num (v, v < 100.0f ? 1 : 0) + " ms";
            case U::seconds:      return num (v, 2) + " s";
            case U::dbPerOctave:  return num (v, 0) + " dB/oktav";
            case U::count:        return juce::String (juce::roundToInt (v));
            case U::plain:        break;
        }
        return num (v, 2);
    }

    float parseValue (const juce::String& text, BuiltInEffect::Unit unit)
    {
        auto t = text.trim().toLowerCase().replaceCharacter (',', '.');
        double mult = 1.0;
        if (unit == BuiltInEffect::Unit::hertz && t.containsChar ('k')) mult = 1000.0;
        if (unit == BuiltInEffect::Unit::milliseconds && t.endsWith ("s") && ! t.endsWith ("ms")) mult = 1000.0;
        return (float) (t.retainCharacters ("-.0123456789").getDoubleValue() * mult);
    }
}
