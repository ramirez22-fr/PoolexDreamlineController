#ifndef __HPCI_H__
#define __HPCI_H__
#include "settings.h"
#include "esphome.h"
#include "esphome/core/component.h"
#include "swi.h"
namespace esphome
{
  namespace hpci
  {
#define HP_FRAME_LEN 16 // Heat pump frame length

    class HeatPumpController : public Component
    {
    public:
      void setup() override;
      volatile bool data_to_send;

      void loop() override;
      void setTargetTemp(uint16_t value);
      void setOn(bool value);
      void setAction(settings::actionEnum value);
      void setOpMode(settings::modeEnum value);
      bool sendControl(settings::ctrlSettings settings);
      float getTargetTemp();
      float getWaterInTemp();
      float getWaterOutTemp();
      float getOutdoorTemp();
      bool getOn();
      bool getRunning();
      uint16_t getErrorCode();
      settings::actionEnum getAction();
      settings::modeEnum getOpMode();

    protected:
      settings::hpInfo hpData;
      settings::ctrlSettings hpSettings;
      bool settings_initialized_; // vrai des que hpSettings a ete aligne sur un etat reel recu
      HighFrequencyLoopRequester high_freq_;
      uint8_t computeChecksum(uint8_t frame[], uint8_t size);
      bool frameIsValid(uint8_t frame[], uint8_t size);

      bool decode(uint8_t frame[]);

      // Optimistic-avec-timeout : la valeur demandee est affichee immediatement,
      // jusqu'a confirmation par une vraie trame recue OU expiration du delai
      // (auquel cas on retombe sur la valeur reelle plutot que de mentir indefiniment).
      static constexpr uint32_t OPTIMISTIC_TIMEOUT_MS = 10000; // ~6s observes + marge

      bool target_temp_pending_ = false;
      unsigned long target_temp_pending_since_ = 0;
      uint8_t target_temp_pending_value_ = 0;

      bool on_pending_ = false;
      unsigned long on_pending_since_ = 0;
      bool on_pending_value_ = false;

      bool op_mode_pending_ = false;
      unsigned long op_mode_pending_since_ = 0;
      settings::modeEnum op_mode_pending_value_ = settings::HEAT_ONLY;

      bool action_pending_ = false;
      unsigned long action_pending_since_ = 0;
      settings::actionEnum action_pending_value_ = settings::HEAT;
    };

  }
}

#endif // __HPCI_H__