#pragma once
#include <Audio/AudioInterface.hpp>
#include <Audio/PortAudioInterface.hpp>
#include <Audio/Settings/Model.hpp>

#include <score/widgets/SignalUtils.hpp>

#include <QComboBox>
#include <QFormLayout>

namespace Audio
{
#if __has_include(<pa_win_wasapi.h>)
class WASAPIFactory final
    : public QObject
    , public AudioFactory
{
  SCORE_CONCRETE("afcd9c64-0367-4fa1-b2bb-ee65b1c5e5a7")
public:
  std::vector<PortAudioCard> devices;

  WASAPIFactory() { rescan(); }

  ~WASAPIFactory() override { }
  bool available() const noexcept override { return true; }
  void
  initialize(Audio::Settings::Model& set, const score::ApplicationContext& ctx) override
  {
    auto device_in = ossia::find_if(devices, [&](const PortAudioCard& dev) {
      return dev.raw_name == set.getCardIn() && dev.hostapi != paInDevelopment;
    });
    auto device_out = ossia::find_if(devices, [&](const PortAudioCard& dev) {
      return dev.raw_name == set.getCardOut() && dev.hostapi != paInDevelopment;
    });

    if(device_in == devices.end() || device_out == devices.end())
    {
      set.setCardIn(devices.back().raw_name);
      set.setCardOut(devices.back().raw_name);
      set.setDefaultIn(devices.back().inputChan);
      set.setDefaultOut(devices.back().outputChan);
      set.setRate(devices.back().rate);

      set.changed();
    }
    else
    {
      if(device_out != devices.end())
      {
        set.setDefaultIn(device_out->inputChan);
        set.setDefaultOut(device_out->outputChan);
        set.setRate(device_out->rate);

        set.changed();
      }
    }
  }

  void rescan()
  {
    devices.clear();
    PortAudioScope portaudio;

    devices.push_back(PortAudioCard{{}, {}, QObject::tr("No device"), -1, 0, 0, {}});
    for(int i = 0; i < Pa_GetHostApiCount(); i++)
    {
      auto hostapi = Pa_GetHostApiInfo(i);
      if(hostapi->type == PaHostApiTypeId::paWASAPI)
      {
        for(int card = 0; card < hostapi->deviceCount; card++)
        {
          auto dev_idx = Pa_HostApiDeviceIndexToDeviceIndex(i, card);
          auto dev = Pa_GetDeviceInfo(dev_idx);

          // WASAPI endpoints are either capture or render, never both.
          // Filtering on maxOutputChannels would drop every microphone,
          // which makes it impossible to select an input device.
          // Keep both and label them, like the ALSA backend does.
          auto raw_name = QString::fromUtf8(dev->name);
          auto pretty_name = raw_name;
          if(dev->maxInputChannels == 0)
            pretty_name = QObject::tr("(Output) ") + pretty_name;
          else if(dev->maxOutputChannels == 0)
            pretty_name = QObject::tr("(Input) ") + pretty_name;

          devices.push_back(PortAudioCard{
              "WASAPI", raw_name, pretty_name, dev_idx, dev->maxInputChannels,
              dev->maxOutputChannels, hostapi->type, dev->defaultSampleRate});
        }
      }
    }
  }

  QString prettyName() const override { return QObject::tr("WASAPI (PortAudio)"); }
  std::shared_ptr<ossia::audio_engine> make_engine(
      const Audio::Settings::Model& set, const score::ApplicationContext& ctx) override
  {
    return std::make_shared<ossia::portaudio_engine>(
        "ossia score", set.getCardIn().toStdString(), set.getCardOut().toStdString(),
        set.getDefaultIn(), set.getDefaultOut(), set.getRate(), set.getBufferSize(),
        paWASAPI);
  }

  void setCard(QComboBox* combo, QString val)
  {
    auto dev_it = ossia::find_if(
        devices, [&](const PortAudioCard& d) { return d.raw_name == val; });
    if(dev_it == devices.end())
      return;

    // Each combo box only holds the devices matching its direction, so the
    // position in `devices` is looked up through the item data.
    const int dev_pos = (int)(dev_it - devices.begin());
    for(int i = 0; i < combo->count(); i++)
    {
      if(combo->itemData(i).toInt() == dev_pos)
      {
        combo->setCurrentIndex(i);
        return;
      }
    }
  }

  QWidget* make_settings(
      Audio::Settings::Model& m, Audio::Settings::View& v,
      score::SettingsCommandDispatcher& m_disp, QWidget* parent) override
  {
    auto w = new QWidget{parent};
    auto lay = new QFormLayout{w};

    // Capture and render are distinct endpoints under WASAPI, so they get
    // one combo box each, like the miniaudio backend does.
    auto card_list_in = new QComboBox{w};
    auto card_list_out = new QComboBox{w};

    // Disabled case
    card_list_in->addItem(devices.front().pretty_name, 0);
    card_list_out->addItem(devices.front().pretty_name, 0);

    // Normal devices
    for(std::size_t i = 1; i < devices.size(); i++)
    {
      auto& card = devices[i];
      if(card.inputChan > 0)
        card_list_in->addItem(card.pretty_name, (int)i);
      if(card.outputChan > 0)
        card_list_out->addItem(card.pretty_name, (int)i);
    }

    using Model = Audio::Settings::Model;

    {
      lay->addRow(QObject::tr("Capture"), card_list_in);

      auto update_dev_in = [=, &m, &m_disp](const PortAudioCard& dev) {
        if(dev.raw_name != m.getCardIn())
        {
          m_disp.submitDeferredCommand<Audio::Settings::SetModelCardIn>(m, dev.raw_name);
          m_disp.submitDeferredCommand<Audio::Settings::SetModelDefaultIn>(
              m, dev.inputChan);
        }
      };

      QObject::connect(
          card_list_in, SignalUtils::QComboBox_currentIndexChanged_int(), &v, [=](int i) {
            auto& device = devices[card_list_in->itemData(i).toInt()];
            update_dev_in(device);
          });

      if(m.getCardIn().isEmpty())
      {
        if(!devices.empty())
        {
          update_dev_in(devices.front());
        }
      }
      else
      {
        setCard(card_list_in, m.getCardIn());
      }
    }

    {
      lay->addRow(QObject::tr("Playback"), card_list_out);

      auto update_dev_out = [=, &m, &m_disp](const PortAudioCard& dev) {
        if(dev.raw_name != m.getCardOut())
        {
          m_disp.submitDeferredCommand<Audio::Settings::SetModelCardOut>(
              m, dev.raw_name);
          m_disp.submitDeferredCommand<Audio::Settings::SetModelDefaultOut>(
              m, dev.outputChan);
        }
      };

      QObject::connect(
          card_list_out, SignalUtils::QComboBox_currentIndexChanged_int(), &v, [=](int i) {
            auto& device = devices[card_list_out->itemData(i).toInt()];
            update_dev_out(device);
          });

      if(m.getCardOut().isEmpty())
      {
        if(!devices.empty())
        {
          update_dev_out(devices.front());
        }
      }
      else
      {
        setCard(card_list_out, m.getCardOut());
      }
    }

    addBufferSizeWidget(*w, m, v);
    addSampleRateWidget(*w, m, v);

    con(m, &Model::changed, w, [=, &m] {
      setCard(card_list_in, m.getCardIn());
      setCard(card_list_out, m.getCardOut());
    });
    return w;
  }
};
#endif
}
