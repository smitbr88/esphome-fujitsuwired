#define USE_CALLBACKS

#include "esphome.h"
#include "esphome/core/preferences.h"

#include "FujiHeatPump.h"

using namespace esphome;

#ifndef FUJITSU_WIRED_H
#define FUJITSU_WIRED_H
#pragma once

	
static const char* TAG = "FujitsuHeatPump"; // Logging tag

static const char* ESPFJHP_VERSION = "0.1";

/* If polling interval is greater than 9 seconds, the HeatPump
library reconnects, but doesn't then follow up with our data request.*/
static const uint32_t ESPFJHP_POLL_INTERVAL_DEFAULT = 3000; // in milliseconds,
                                                           // 0 < X <= 9000
static const uint8_t ESPFJHP_MIN_TEMPERATURE = 16; // degrees C,
                                                  // defined by hardware
static const uint8_t ESPFJHP_MAX_TEMPERATURE = 31; // degrees C,
                                                  //defined by hardware
static const float   ESPFJHP_TEMPERATURE_STEP = 1; // temperature setting step,
                                                    // in degrees C

class FujitsuHeatPump : public PollingComponent, public climate::Climate {

	public:

			
        /**
         * Create a new FujitsuHeatPump object
         *
         * Args:
         *   hw_serial: pointer to an Arduino HardwareSerial instance
         *   poll_interval: polling interval in milliseconds
         */
        FujitsuHeatPump(
            HardwareSerial* hw_serial,
            uint32_t poll_interval=ESPFJHP_POLL_INTERVAL_DEFAULT
        );

        // Print a banner with library information.
        void banner() {
            ESP_LOGI(TAG, "ESPHome FujitsuHeatPump version %s",
                    ESPFJHP_VERSION);
        }

        // print the current configuration
        void dump_config() override;

        // handle a change in settings as detected by the HeatPump library.
        void hpSettingsChanged();

        // Handle a change in status as detected by the HeatPump library.
        void hpStatusChanged(FujiStatus currentStatus);

        // Set up the component, initializing the HeatPump object.
        void setup() override;

        // This is called every poll_interval.
        void update() override;

        // Configure the climate object with traits that we support.
        climate::ClimateTraits traits() override;

        // Get a mutable reference to the traits that we support.
        climate::ClimateTraits& config_traits();

        // Debugging function to print the object's state.
        void dump_state();

        // Handle a request from the user to change settings.
        void control(const climate::ClimateCall &call) override;

        // Use the temperature from an external sensor. Use
        // set_remote_temp(0) to switch back to the internal sensor.
        void set_remote_temperature(float);

    protected:

        // The ClimateTraits supported by this HeatPump.
        climate::ClimateTraits traits_;

        // Allow the HeatPump class to use get_hw_serial_
        friend class FujiHeatPump;




    private:
        // Retrieve the HardwareSerial pointer from friend and subclasses.



};

#endif