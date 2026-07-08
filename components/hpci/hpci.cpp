#include "hpci.h"

namespace esphome
{
    namespace hpci
    {
        uint8_t lastDataType;
        volatile bool data_to_send = false;

        void HeatPumpController::setup()
        {
            lastDataType = 0;
            this->high_freq_.start();

            // IMPORTANT : force explicitement a false. Sans cette ligne, la valeur de
            // depart de data_to_send n'est pas garantie a false selon la construction
            // de l'objet, ce qui peut declencher un envoi involontaire des reglages
            // fige ci-dessous vers la vraie PAC des les premieres iterations de loop().
            this->data_to_send = false;
            this->settings_initialized_ = false;

            settings::ctrlSettings defaultSettings = {
                29,                  // uint8_t targetTemp;
                40,                  // uint8_t defrostAutoEnableTime;
                7,                   // uint8_t defrostEnableTemp;
                13,                  // uint8_t defrostDisableTemp;
                8,                   // uint8_t defrostMaxDuration;
                2,                   // uint8_t restartOffsetTemp;
                0,                   // uint8_t compressorStopMarginTemp;
                118,                 // uint8_t thermalProtection;
                40,                  // uint8_t maximumTemp;
                15,                  // uint8_t stopWhenReachedDelay;
                true,                // bool specialCtrlMode;
                false,               // bool on;
                settings::HEAT,      // actionEnum action;
                true,                // bool autoRestart;
                settings::HEAT_ONLY, // modeEnum opMode;
            };
            // Ces valeurs ne servent plus que de filet de securite pour la toute petite
            // fenetre avant la premiere trame reelle recue (quelques secondes) : elles
            // sont remplacees par l'etat reel de la PAC des que decode() en recoit une
            // (voir plus bas), et ne sont jamais envoyees automatiquement puisque
            // data_to_send reste a false tant que l'utilisateur n'a rien demande.
            this->hpSettings = defaultSettings;
            swi::swi_setup();
            ESP_LOGI("HPCI", "Successful setup!");
        }

        void HeatPumpController::loop()
        {
            swi::swi_loop();

            if (swi::frame_available)
            {
                swi::frame_available = false;
                if (this->frameIsValid(swi::read_frame, swi::frameCnt))
                {
                    this->decode(swi::read_frame);
                    ESP_LOGI("HPCI", "Frame Data (%s, header 0x%02X):",
                             (lastDataType == 0xD2 || lastDataType == 0xCC) ? "Control" : "Status", lastDataType);
                    if (lastDataType == 0xD2 || lastDataType == 0xCC)
                    {
                        ESP_LOGD("HPCI", "PAC %s, target: %d", (this->hpData.on ? "ON" : "OFF"), this->hpData.targetTemp);
                        ESP_LOGD("HPCI", "Defrost Auto Enable Time %d, Defrost Enable Temp: %d", this->hpData.defrostAutoEnableTime, this->hpData.defrostEnableTemp);
                        ESP_LOGD("HPCI", "Defrost Disable Temp %d, Defrost Max Duration: %d", this->hpData.defrostDisableTemp, this->hpData.defrostMaxDuration);
                    }
                    else if (lastDataType == 0xDD)
                    {
                        ESP_LOGD("HPCI", "Water temp IN %d, Water temp OUT: %d", this->hpData.waterTempIn, this->hpData.waterTempOut);
                        ESP_LOGD("HPCI", "Air Outlet Temp: %d, Outdoor Air Temp: %d", this->hpData.airOutletTemp, this->hpData.outdoorAirTemp);
                        ESP_LOGD("HPCI", "Error Code %d, Time Since Fan: %d", this->hpData.errorCode, this->hpData.timeSinceFan);
                        ESP_LOGD("HPCI", "Time Since Pump %d", this->hpData.timeSincePump);
                    }
                }
                else
                {
                    ESP_LOGW("HPCI", "Invalid or corrupt frame");
                }
            }
            if (this->data_to_send)
            {
                if (this->sendControl(this->hpSettings))
                {
                    this->data_to_send = false;
                }
                // Wait for the data to be sent. Data will be sent when transmission is available.
            }
        }

        bool HeatPumpController::sendControl(settings::ctrlSettings settings)
        {
            uint8_t frame[HP_FRAME_LEN];
            frame[0] = 0xCC; // header
            frame[1] = 0x0C; // header
            frame[2] = settings.targetTemp;
            frame[3] = settings.defrostAutoEnableTime;
            frame[4] = settings.defrostEnableTemp;
            frame[5] = settings.defrostDisableTemp;
            frame[6] = settings.defrostMaxDuration * 20;
            // MODE 1
            frame[7] = 0x00; // Clear any current flag
            frame[7] |= (settings.specialCtrlMode ? 0x80 : 0x00);
            frame[7] |= (settings.on ? 0x40 : 0x00);
            frame[7] |= ((settings.action == settings::HEAT) ? 0x20 : 0x00);
            frame[7] |= (settings.autoRestart ? 0x08 : 0x00);
            frame[7] |= ((int)settings.opMode & 0x03) << 1;
            frame[8] = 0x1D; // MODE 2 (NOT A HYBRID PUMP)
            frame[9] = settings.restartOffsetTemp;
            frame[10] = settings.compressorStopMarginTemp;
            frame[11] = settings.thermalProtection;
            frame[12] = settings.maximumTemp;
            frame[13] = settings.stopWhenReachedDelay;
            frame[14] = 0x00; // SCHEDULE SETTING OFF
            frame[15] = this->computeChecksum(frame, HP_FRAME_LEN);
            return swi::sendFrame(frame, HP_FRAME_LEN);
        }

        bool HeatPumpController::decode(uint8_t frame[])
        {
            if (frame[0] == 0xD2 || frame[0] == 0xCC)
            {
                // 0xD2 : echo des reglages de controle par la carte mere
                // 0xCC : meme structure de trame, emise par le boitier/telecommande filaire
                //        (identifie apres complement dans frameIsValid())
                this->hpData.targetTemp = frame[2];
                this->hpData.defrostAutoEnableTime = frame[3];
                this->hpData.defrostEnableTemp = frame[4];
                this->hpData.defrostDisableTemp = frame[5];
                this->hpData.defrostMaxDuration = frame[6] / 20;
                this->hpData.on = frame[7] & 0x40;
                this->hpData.autoRestart = frame[7] & 0x08;
                // bug corrige : il faut decaler le masque avant de le transtyper en enum
                this->hpData.opMode = static_cast<settings::modeEnum>((frame[7] & 0x06) >> 1);
                this->hpData.action = static_cast<settings::actionEnum>((frame[7] & 0x20) ? settings::HEAT : settings::COOL);
                this->hpData.specialCtrlMode = frame[7] & 0x80;
                this->hpData.restartOffsetTemp = frame[9];
                this->hpData.compressorStopMarginTemp = frame[10];
                this->hpData.thermalProtection = frame[11];
                this->hpData.maximumTemp = frame[12];
                this->hpData.stopWhenReachedDelay = frame[13];
                // this->hpData.targetTemp = frame[14];

                if (!this->settings_initialized_)
                {
                    // Premiere trame Control reelle recue : on aligne les reglages de
                    // depart (hpSettings) sur l'etat reel de la PAC plutot que de
                    // garder les valeurs figees de setup(). Ainsi, si l'utilisateur ne
                    // change qu'un seul reglage (ex. la temperature), sendControl()
                    // n'ecrasera pas silencieusement le mode/l'etat marche-arret reels.
                    this->hpSettings.targetTemp = this->hpData.targetTemp;
                    this->hpSettings.defrostAutoEnableTime = this->hpData.defrostAutoEnableTime;
                    this->hpSettings.defrostEnableTemp = this->hpData.defrostEnableTemp;
                    this->hpSettings.defrostDisableTemp = this->hpData.defrostDisableTemp;
                    this->hpSettings.defrostMaxDuration = this->hpData.defrostMaxDuration;
                    this->hpSettings.specialCtrlMode = this->hpData.specialCtrlMode;
                    this->hpSettings.on = this->hpData.on;
                    this->hpSettings.action = this->hpData.action;
                    this->hpSettings.autoRestart = this->hpData.autoRestart;
                    this->hpSettings.opMode = this->hpData.opMode;
                    this->hpSettings.restartOffsetTemp = this->hpData.restartOffsetTemp;
                    this->hpSettings.compressorStopMarginTemp = this->hpData.compressorStopMarginTemp;
                    this->hpSettings.thermalProtection = this->hpData.thermalProtection;
                    this->hpSettings.maximumTemp = this->hpData.maximumTemp;
                    this->hpSettings.stopWhenReachedDelay = this->hpData.stopWhenReachedDelay;
                    this->settings_initialized_ = true;
                    ESP_LOGI("HPCI", "Reglages initiaux alignes sur l'etat reel de la PAC");
                }

                lastDataType = frame[0];
                return true;
            }
            else if (frame[0] == 0xDD)
            {
                this->hpData.waterTempIn = frame[1];
                this->hpData.waterTempOut = frame[2];
                this->hpData.coilTemp = frame[3];
                this->hpData.airOutletTemp = frame[4];
                this->hpData.outdoorAirTemp = frame[5];
                this->hpData.errorCode = static_cast<settings::hpErrorEnum>(frame[7]);
                this->hpData.timeSinceFan = frame[10];
                this->hpData.timeSincePump = frame[11];
                this->hpData.maximumTemp = frame[12];
                this->hpData.stopWhenReachedDelay = frame[13];
                lastDataType = 0xDD;
                return true;
            }
            ESP_LOGW("HPCI", "UNKNOWN MESSAGE !");
            return false;
        }

        void HeatPumpController::setOn(bool value)
        {
            this->hpSettings.on = value;
            this->data_to_send = true;
            this->on_pending_ = true;
            this->on_pending_value_ = value;
            this->on_pending_since_ = millis();
        }
        void HeatPumpController::setAction(settings::actionEnum value)
        {
            this->hpSettings.action = value;
            this->data_to_send = true;
            this->action_pending_ = true;
            this->action_pending_value_ = value;
            this->action_pending_since_ = millis();
        }
        void HeatPumpController::setOpMode(settings::modeEnum value)
        {
            this->hpSettings.opMode = value;
            this->data_to_send = true;
            this->op_mode_pending_ = true;
            this->op_mode_pending_value_ = value;
            this->op_mode_pending_since_ = millis();
        }
        settings::actionEnum HeatPumpController::getAction()
        {
            if (this->action_pending_)
            {
                if (this->hpData.action == this->action_pending_value_)
                {
                    this->action_pending_ = false; // confirme par une vraie trame
                }
                else if (millis() - this->action_pending_since_ > OPTIMISTIC_TIMEOUT_MS)
                {
                    this->action_pending_ = false; // pas confirme a temps, on abandonne
                }
                else
                {
                    return this->action_pending_value_; // encore dans la fenetre optimiste
                }
            }
            return this->hpData.action;
        }
        settings::modeEnum HeatPumpController::getOpMode()
        {
            if (this->op_mode_pending_)
            {
                if (this->hpData.opMode == this->op_mode_pending_value_)
                {
                    this->op_mode_pending_ = false;
                }
                else if (millis() - this->op_mode_pending_since_ > OPTIMISTIC_TIMEOUT_MS)
                {
                    this->op_mode_pending_ = false;
                }
                else
                {
                    return this->op_mode_pending_value_;
                }
            }
            return this->hpData.opMode;
        }
        void HeatPumpController::setTargetTemp(uint16_t value)
        {
            this->hpSettings.targetTemp = value;
            this->data_to_send = true;
            this->target_temp_pending_ = true;
            this->target_temp_pending_value_ = (uint8_t)value;
            this->target_temp_pending_since_ = millis();
        }
        float HeatPumpController::getTargetTemp()
        {
            if (this->target_temp_pending_)
            {
                if (this->hpData.targetTemp == this->target_temp_pending_value_)
                {
                    this->target_temp_pending_ = false;
                }
                else if (millis() - this->target_temp_pending_since_ > OPTIMISTIC_TIMEOUT_MS)
                {
                    this->target_temp_pending_ = false;
                }
                else
                {
                    return (float)this->target_temp_pending_value_;
                }
            }
            return (float)this->hpData.targetTemp;
        }
        float HeatPumpController::getWaterInTemp()
        {

            return (float)this->hpData.waterTempIn;
        }

        float HeatPumpController::getWaterOutTemp()
        {
            return this->hpData.waterTempOut;
        }

        float HeatPumpController::getOutdoorTemp()
        {
            return (float)this->hpData.outdoorAirTemp;
        }

        bool HeatPumpController::getOn()
        {
            if (this->on_pending_)
            {
                if (this->hpData.on == this->on_pending_value_)
                {
                    this->on_pending_ = false;
                }
                else if (millis() - this->on_pending_since_ > OPTIMISTIC_TIMEOUT_MS)
                {
                    this->on_pending_ = false;
                }
                else
                {
                    return this->on_pending_value_;
                }
            }
            return this->hpData.on;
        }

        bool HeatPumpController::getRunning()
        {
            return (this->hpData.timeSinceFan > 0);
        }

        uint16_t HeatPumpController::getErrorCode()
        {
            return this->hpData.errorCode;
        }

        bool HeatPumpController::frameIsValid(uint8_t frame[], uint8_t size)
        {
            if (size != HP_FRAME_LEN)
            {
                ESP_LOGW("HPCI", "Frame size is not valid (%d)", size);
                return false;
            }

            // 1) essai direct : trames emises par la carte mere (entetes 0xD2 / 0xDD observes en clair)
            unsigned char computed_checksum = this->computeChecksum(frame, size);
            unsigned char checksum = frame[size - 1];
            if (computed_checksum == checksum)
            {
                return true;
            }

            // 2) essai en complement a 1 : trames emises par le boitier/telecommande filaire
            //    (observe empiriquement : entete 0xCC/0x0C qui n'apparait qu'apres complement)
            uint8_t inverted[HP_FRAME_LEN];
            for (uint8_t i = 0; i < size; i++)
            {
                inverted[i] = (uint8_t)(~frame[i]);
            }
            unsigned char computed_inverted = this->computeChecksum(inverted, size);
            if (computed_inverted == inverted[size - 1])
            {
                for (uint8_t i = 0; i < size; i++)
                {
                    frame[i] = inverted[i];
                }
                ESP_LOGD("HPCI", "Frame valid after complement (controller-side frame)");
                return true;
            }

            ESP_LOGD("HPCI", "Checksum mismatch: raw %d/%d, inverted %d/%d",
                     checksum, computed_checksum, inverted[size - 1], computed_inverted);
            return false;
        }

        uint8_t HeatPumpController::computeChecksum(uint8_t frame[], uint8_t size)
        {
            unsigned int total = 0;
            for (byte i = 1; i < size - 1; i++)
            {
                total += frame[i];
            }
            byte checksum = total % 256;
            return checksum;
        }

    }
}