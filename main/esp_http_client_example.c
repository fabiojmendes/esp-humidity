/* ESP HTTP Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include <bme680.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define MAX_HTTP_POST 512
static const char *TAG = "HTTP_CLIENT";

#define SDA_GPIO 8
#define SCL_GPIO 7
#define PORT 0
#define ADDR BME680_I2C_ADDR_1

static void http_rest_with_hostname_path(bme680_values_float_t *values) {
  esp_http_client_config_t config = {
      .host = "linux-desktop.local",
      .port = 8086,
      .path = "/write?db=hum",
      .method = HTTP_METHOD_POST,
      .transport_type = HTTP_TRANSPORT_OVER_TCP,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);

  // POST
  char post_data[MAX_HTTP_POST];
  snprintf(post_data, MAX_HTTP_POST,
           "sensor,location=office temperature=%.2f\n"
           "sensor,location=office humidity=%.2f\n"
           "sensor,location=office pressure=%.2f\n",
           values->temperature, values->humidity, values->pressure);
  esp_http_client_set_post_field(client, post_data, strlen(post_data));
  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGD(TAG, "HTTP POST Status = %d, content_length = %lld",
             esp_http_client_get_status_code(client),
             esp_http_client_get_content_length(client));
  } else {
    ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
}

void bme680_test(void *pvParameters) {
  bme680_t sensor = {};

  ESP_LOGI(TAG, "Init BME 680 at %X, %X", ADDR, PORT);
  ESP_ERROR_CHECK(bme680_init_desc(&sensor, ADDR, PORT, SDA_GPIO, SCL_GPIO));

  // init the sensor
  ESP_ERROR_CHECK(bme680_init_sensor(&sensor));

  // // Changes the oversampling rates to 4x oversampling for temperature
  // // and 2x oversampling for humidity. Pressure measurement is skipped.
  // bme680_set_oversampling_rates(&sensor, BME680_OSR_4X, BME680_OSR_NONE,
  //                               BME680_OSR_2X);

  // // Change the IIR filter size for temperature and pressure to 7.
  // bme680_set_filter_size(&sensor, BME680_IIR_SIZE_7);

  // // Change the heater profile 0 to 200 degree Celsius for 100 ms.
  // bme680_set_heater_profile(&sensor, 0, 200, 100);
  bme680_use_heater_profile(&sensor, -1);

  // // Set ambient temperature to 10 degree Celsius
  // bme680_set_ambient_temperature(&sensor, 10);

  // as long as sensor configuration isn't changed, duration is constant
  uint32_t duration;
  bme680_get_measurement_duration(&sensor, &duration);

  TickType_t last_wakeup = xTaskGetTickCount();

  bme680_values_float_t values;
  while (true) {
    // trigger the sensor to start one TPHG measurement cycle
    ESP_ERROR_CHECK(bme680_force_measurement(&sensor));
    // passive waiting until measurement results are available
    vTaskDelay(duration);

    // get the results and do something with them
    ESP_ERROR_CHECK(bme680_get_results_float(&sensor, &values));
    ESP_LOGI(TAG, "BME680 Sensor: %.2f °C, %.2f %%, %.2f hPa, %.2f Ohm",
             values.temperature, values.humidity, values.pressure,
             values.gas_resistance);

    http_rest_with_hostname_path(&values);
    // passive waiting until 10 seconds is over
    vTaskDelayUntil(&last_wakeup, pdMS_TO_TICKS(10000));
  }
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  ESP_ERROR_CHECK(example_connect());
  ESP_LOGI(TAG, "Connected to AP, begin http example");

  ESP_ERROR_CHECK(i2cdev_init());

  xTaskCreate(bme680_test, "bme680_test", 8192, NULL, 5, NULL);
}
