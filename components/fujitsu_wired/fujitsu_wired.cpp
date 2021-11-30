#include "fujitsu_wired.h"  


using namespace esphome;

#define RX_PIN 16
#define TX_PIN 17

FujiHeatPump hp;

TaskHandle_t FujiTaskLoop;

typedef struct FujiStatus {
    byte isBound = 0;
    byte onOff = 0;
    byte acMode = 5;
    byte temperature = 16;
    byte fanMode = 0;
    byte controllerTemp = 16;
    byte acError = 0;
} FujiStatus;

FujiStatus sharedState;
FujiStatus pendingState;
byte       pendingFields = 0;
bool       hpIsBound = false;

SemaphoreHandle_t updateMutex;
SemaphoreHandle_t pendingMutex;

/**
 * Create a new FujitsuHeatPump object
 *
 * Args:
 *   hw_serial: pointer to an Arduino HardwareSerial instance
 *   poll_interval: polling interval in milliseconds
 */
FujitsuHeatPump::FujitsuHeatPump(
        uint32_t poll_interval
) :
    PollingComponent{poll_interval}, // member initializers list
    

{
    this->traits_.set_supports_current_temperature(true);
    this->traits_.set_visual_min_temperature(ESPFJHP_MIN_TEMPERATURE);
    this->traits_.set_visual_max_temperature(ESPFJHP_MAX_TEMPERATURE);
    this->traits_.set_visual_temperature_step(ESPFJHP_TEMPERATURE_STEP);
    FujiStatus currentState;
    
}

void FujitsuHeatPump::check_logger_conflict_() {
#ifdef USE_LOGGER
    if (this->get_hw_serial_() == logger::global_logger->get_hw_serial()) {
        ESP_LOGW(TAG, "  You're using the same serial port for logging"
                " and the FujitsuHeatPump component. Please disable"
                " logging over the serial port by setting"
                " logger:baud_rate to 0.");
    }
#endif
}

byte convertEspModeToACMode(int mode) {
    switch(mode) {
        case static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_COOL):
          return static_cast<byte>(FujiMode::COOL);          
        case static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_HEAT):
          return static_cast<byte>(FujiMode::HEAT);
        case static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_HEAT_COOL):
          return static_cast<byte>(FujiMode::AUTO);
        case static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_DRY):
          return static_cast<byte>(FujiMode::DRY);
        case static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_FAN_ONLY):
          return static_cast<byte>(FujiMode::FAN);

    } return 0;
}

int convertACFanModeToEspFanMode(byte fan_mode) {
    switch(fan_mode) {
        case static_cast<byte>(FujiFanMode::FAN_AUTO):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_FAN_AUTO);
          break;
        case static_cast<byte>(FujiFanMode::FAN_HIGH):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_FAN_HIGH);
          break;
        case static_cast<byte>(FujiFanMode::FAN_MEDIUM):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_FAN_MEDIUM);
          break;
        case static_cast<byte>(FujiFanMode::FAN_LOW):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_FAN_LOW);
      }
    return 2;

}

int convertACModeToEspMode(byte acMode) {  
    switch(acMode) {
        case static_cast<byte>(FujiMode::COOL):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_COOL);
          break;
        case static_cast<byte>(FujiMode::HEAT):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_HEAT);
          break;
        case static_cast<byte>(FujiMode::AUTO):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_HEAT_COOL);
          break;
        case static_cast<byte>(FujiMode::FAN):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_FAN_ONLY);
          break;
        case static_cast<byte>(FujiMode::DRY):
          return static_cast<int>(FujitsuHeatPump::CLIMATE_MODE_DRY);
      }
    return 6;
};

/**
 * Get our supported traits.
 *
 * Note:
 * Many of the following traits are only available in the 1.5.0 dev train of
 * ESPHome, particularly the Dry operation mode, and several of the fan modes.
 *
 * Returns:
 *   This class' supported climate::ClimateTraits.
 */
climate::ClimateTraits FujitsuHeatPump::traits() {
    return traits_;
}

void FujitsuHeatPump::update() {
    FujiStatus currentState;
    FujiFrame fd;
    bool updated = false;
        // This will be called every "update_interval" milliseconds.
    //this->dump_config();

    if(pendingFields) { // if there are pending updates, use those instead to avoid UI bounces
        if(xSemaphoreTake(pendingMutex, (TickType_t) 200 ) == pdTRUE) {
          memcpy(&currentState, &pendingState, sizeof(FujiStatus));          
          xSemaphoreGive(pendingMutex);
        } else {
          Serial.printf("Failed to get MUTEX lock, skipping loop\n");
          return;
        }
    } else if(xSemaphoreTake( updateMutex, ( TickType_t ) 200 ) == pdTRUE) {
        memcpy(&currentState, &sharedState, sizeof(FujiStatus));
        xSemaphoreGive( updateMutex);
      } else {
        Serial.printf("Failed to get MUTEX lock, skipping loop\n"); 
        return;
    }

    if (currentState.isBound) {        
        int convertedAcMode = convertACModeToEspMode(currentState.acMode);
        int convertedACFanMode = convertedACFanMode(currentState.fanMode);
        if (mode != currentState.onOff) {
            this->mode = convertedAcMode;
            updated = true;
        }
        if (mode != convertedAcMode) {
            this->mode = convertedAcMode;
            updated = true;
        }
        if (fan_mode != convertedACFanMode) {
            this->fan_mode = convertedACFanMode;
            updated = true;
        }
        if (target_temperature != currentState.temperature) {
            this->target_temperature = currentState.temperature;
            updated = true;
        }
        if (updated) {
            publish_state();
        }
    } else {
        ESP_LOGW("Error", " mSrc: %d mDst: %d mType: %d write: %d login: %d unknown: %d onOff: %d temp: %d, mode: %d cP:%d uM:%d cTemp:%d acError:%d \n", fd.messageSource, fd.messageDest, fd.messageType, fd.writeBit, fd.loginBit, fd.unknownBit, fd.onOff, fd.temperature, fd.acMode, fd.controllerPresent, fd.updateMagic, fd.controllerTemp, fd.acError);
    }



/**
 * Implement control of a FujitsuHeatPump.
 *
 * Maps HomeAssistant/ESPHome modes to Mitsubishi modes.
 */
void FujitsuHeatPump::control(const climate::ClimateCall &call) {
    ESP_LOGV(TAG, "Control called.");

    bool cUpdated = false;
    bool cHas_mode = call.get_mode().has_value();
    bool cHas_temp = call.get_target_temperature().has_value();
    

    if (cHas_mode){
        this->mode = *call.get_mode();
    }
    switch (this->mode) {
        case climate::CLIMATE_MODE_OFF:
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 0;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
        break;
        case climate::CLIMATE_MODE_COOL:
            if (mode > 0) {
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 1;
                xSemaphoreGive(pendingMutex);
            }
            if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
                pendingFields |= kModeUpdateMask;
                pendingState.acMode = FujiMode::COOL;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
            break;
        case climate::CLIMATE_MODE_HEAT:
            if (mode > 0) {
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 1;
                xSemaphoreGive(pendingMutex);
            }
            if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
                pendingFields |= kModeUpdateMask;
                pendingState.acMode = FujiMode::HEAT;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            if (mode > 0) {
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 1;
                xSemaphoreGive(pendingMutex);
            }
            if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
                pendingFields |= kModeUpdateMask;
                pendingState.acMode = FujiMode::DRY;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
            break;
        case climate::CLIMATE_MODE_HEAT_COOL:
            if (mode > 0) {
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 1;
                xSemaphoreGive(pendingMutex);
            }
            if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
                pendingFields |= kModeUpdateMask;
                pendingState.acMode = FujiMode::AUTO;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            if (mode > 0) {
            if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                pendingFields |= kOnOffUpdateMask;
                pendingState.onOff = 1;
                xSemaphoreGive(pendingMutex);
            }
            if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
                pendingFields |= kModeUpdateMask;
                pendingState.acMode = FujiMode::FAN;
                xSemaphoreGive(pendingMutex);
            } cUpdated = true;
            break;
        case climate::CLIMATE_MODE_OFF:
        default:
            if (has_mode){
                if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                    pendingFields |= kOnOffUpdateMask;
                    pendingState.onOff = 0;
                    xSemaphoreGive(pendingMutex);
                    cUpdated = true;
            }
            break;
    }

    if (has_temp){
        ESP_LOGV(
            "control", "Sending target temp: %.1f",
            *call.get_target_temperature()
        );
        this->target_temperature = *call.get_target_temperature();
        if(xSemaphoreTake(pendingMutex, (TickType_t) 200 ) == pdTRUE) {
            pendingFields |= kTempUpdateMask;
            pendingState.temperature = target_temperature; // set to the midpoint
            xSemaphoreGive(pendingMutex);
        cUpdated = true;
    }

    //const char* FAN_MAP[6]         = {"AUTO", "QUIET", "1", "2", "3", "4"};
    if (call.get_fan_mode().has_value()) {
        ESP_LOGV("control", "Requested fan mode is %s", *call.get_fan_mode());
        this->fan_mode = *call.get_fan_mode();
        switch(*call.get_fan_mode()) {
            case climate::CLIMATE_FAN_OFF:
                if(xSemaphoreTake(pendingMutex, (TickType_t)200 ) == pdTRUE) {
                    pendingFields |= kOnOffUpdateMask;
                    pendingState.onOff = 0;
                    xSemaphoreGive(pendingMutex);
                }
                cUpdated = true;
                break;
            case climate::CLIMATE_FAN_DIFFUSE:
                if(xSemaphoreTake(pendingMutex, ( TickType_t ) 200 ) == pdTRUE) {
                pendingFields |= kFanModeUpdateMask;
                pendingState.fanMode = FujiFanMode::QUIET;
                }
                cUpdated = true;
                break;
            case climate::CLIMATE_FAN_LOW:
                if(xSemaphoreTake(pendingMutex, ( TickType_t ) 200 ) == pdTRUE) {
                pendingFields |= kFanModeUpdateMask;
                pendingState.fanMode = FujiFanMode::LOW;
                }
                cUpdated = true;
                break;
            case climate::CLIMATE_FAN_MEDIUM:
                if(xSemaphoreTake(pendingMutex, ( TickType_t ) 200 ) == pdTRUE) {
                pendingFields |= kFanModeUpdateMask;
                pendingState.fanMode = FujiFanMode::MEDIUM;
                }
                cUpdated = true;
                break;
            case climate::CLIMATE_FAN_HIGH:
                if(xSemaphoreTake(pendingMutex, ( TickType_t ) 200 ) == pdTRUE) {
                pendingFields |= kFanModeUpdateMask;
                pendingState.fanMode = FujiFanMode::HIGH;
                }
                cUpdated = true;
                break;
            case climate::CLIMATE_FAN_ON:
            case climate::CLIMATE_FAN_AUTO:
            default:
                if(xSemaphoreTake(pendingMutex, ( TickType_t ) 200 ) == pdTRUE) {
                pendingFields |= kFanModeUpdateMask;
                pendingState.fanMode = FujiFanMode::AUTO;
                }
                cUpdated = true;
                break;
            }
        if (cUpdated) {
            this->publish_state();

        }
    }



void FujiTaskLoop(void *pvParameters){
  FujiHeatPump hp;
  FujiStatus updateState;
  byte updatedFields = 0;

  Serial.print("FujiTask Begin on core ");
  Serial.println(xPortGetCoreID());

  Serial.print("Creating FujiHeatPump object\n");
  hp.connect(&Serial2, true, RX_PIN, TX_PIN);
    
  for(;;){
      if(xSemaphoreTake(pendingMutex, (TickType_t)200) == pdTRUE) {
        if(pendingFields) {
          memcpy(&updateState, &pendingState, sizeof(FujiStatus));
          updatedFields = pendingFields;
          pendingFields = 0;
        }
        xSemaphoreGive(pendingMutex);
      }

      // apply updated fields
      if(updatedFields & kOnOffUpdateMask) {
          hp.setOnOff(updateState.onOff == 1 ? true : false);
      }
      
      if(updatedFields & kTempUpdateMask) {
          hp.setTemp(updateState.temperature);
      }
      
      if(updatedFields & kModeUpdateMask) {
          hp.setMode(updateState.acMode);
      }
      
      if(updatedFields & kFanModeUpdateMask) {
          hp.setFanMode(updateState.fanMode);
      }
      
      if(updatedFields & kSwingModeUpdateMask) {
          hp.setSwingMode(updateState.swingMode);
      }
      
      if(updatedFields & kSwingStepUpdateMask) {
          hp.setSwingStep(updateState.swingStep);
      }

      updatedFields = 0;
  
      if(hp.waitForFrame()) {
        delay(60);
        hp.sendPendingFrame();

        updateState.isBound        = hp.isBound();;
        updateState.onOff          = hp.getCurrentState()->onOff;
        updateState.acMode         = hp.getCurrentState()->acMode;
        updateState.temperature    = hp.getCurrentState()->temperature;
        updateState.fanMode        = hp.getCurrentState()->fanMode;
        updateState.controllerTemp = hp.getCurrentState()->controllerTemp;

        if(xSemaphoreTake(updateMutex, (TickType_t)200) == pdTRUE) {
          memcpy(&sharedState, &updateState, sizeof(FujiStatus)); //copy our local back to the shared
          xSemaphoreGive(updateMutex);
        }
      }

      hpIsBound = hp.isBound();
      
      delay(1);
  } 
}

void FujitsuHeatPump::setup() {
    // This will be called by App.setup()

    this->   xTaskCreatePinnedToCore(
                    FujiTaskLoop,   /* Task function. */
                    "FujiTask",     /* name of task. */
                    10000,          /* Stack size of task */
                    NULL,           /* parameter of the task */
                    19,             /* priority of the task */
                    &FujiTask,      /* Task handle to keep track of created task */
                    0);             /* pin task to core 0 */   

    this->check_logger_conflict_();

    ESP_LOGCONFIG(TAG, "Intializing new HeatPump object.");
    this->current_temperature = NAN;
    this->target_temperature = NAN;
    this->fan_mode = climate::CLIMATE_FAN_OFF;
    this->swing_mode = climate::CLIMATE_SWING_OFF;

#ifdef USE_CALLBACKS
    hp->setSettingsChangedCallback(
            [this]() {
                this->hpSettingsChanged();
            }
    );

    hp->setStatusChangedCallback(
            [this](FujiStatus currentStatus) {
                this->FujiStatus(updateState);
            }
    );
#endif



}