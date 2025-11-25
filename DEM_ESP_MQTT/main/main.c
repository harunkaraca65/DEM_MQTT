/*
===============================================================================
 Project: ESP-IDF Wi-Fi + MQTT Smart Setup (Auto/Manual)
-------------------------------------------------------------------------------
 @brief
   Smart IoT application with NVS persistence.
   - [O] Auto Connect: Loads credentials from NVS and connects.
   - [N] New Setup: Runs interactive wizard to get new credentials.

 @details
   - Persistent storage (NVS) for Wi-Fi & MQTT settings.
   - Interactive UART menu at boot.
   - Automatic reconnection logic.
   - Periodic data publishing.

 Author:  Harun Karaca
 Date:    12-11-2025
===============================================================================
*/

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//=============================================================================
// Definitions
//=============================================================================
#define TAG "SMART_APP"
#define UART_PORT_NUM UART_NUM_0

//=============================================================================
// Global Variables
//=============================================================================
static bool wifi_connected = false;
static bool mqtt_connected = false;
static esp_mqtt_client_handle_t client;

// Buffers for credentials
char ssid[32] = {0};
char wifi_pass[64] = {0};
char mqtt_broker[64] = {0};
char mqtt_topic[64] = {0};

//=============================================================================
// NVS Helper Functions
//=============================================================================
/**
 * @brief Saves a string to NVS storage.
 */
esp_err_t save_to_nvs(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

/**
 * @brief Loads a string from NVS storage.
 */
esp_err_t load_from_nvs(const char *key, char *buffer, size_t max_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t required_size;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err == ESP_OK && required_size <= max_len) {
        err = nvs_get_str(handle, key, buffer, &required_size);
    }
    nvs_close(handle);
    return err;
}

//=============================================================================
// UART Input Function
//=============================================================================
void read_input(const char *prompt, char *buffer, size_t len, bool mask) {
    printf("%s", prompt);
    fflush(stdout);
    memset(buffer, 0, len);
    int i = 0;
    uint8_t ch;

    while (i < len - 1) {
        int read_len = uart_read_bytes(UART_PORT_NUM, &ch, 1, pdMS_TO_TICKS(50));
        if (read_len > 0) {
            if (ch == '\r' || ch == '\n') {
                uart_write_bytes(UART_PORT_NUM, "\n", 1);
                break;
            }
            if (ch == '\b' || ch == 127) {
                if (i > 0) { i--; uart_write_bytes(UART_PORT_NUM, "\b \b", 3); }
                continue;
            }
            if (ch >= 32 && ch <= 126) {
                buffer[i++] = (char)ch;
                uart_write_bytes(UART_PORT_NUM, mask ? "*" : (const char *)&ch, 1);
            }
        }
    }
    buffer[i] = '\0';
}

//=============================================================================
// Event Handlers
//=============================================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        // Simple auto-reconnect logic could be added here if needed
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi Connected! IP Obtained.");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT Connected.");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT Disconnected.");
    }
}

//=============================================================================
// Connection Logic
//=============================================================================
void wifi_stack_init(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
}

esp_err_t attempt_wifi_connect(void) {
    wifi_connected = false;
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, wifi_pass, sizeof(wifi_config.sta.password));
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    // Wait up to 8 seconds
    int attempts = 0;
    while (!wifi_connected && attempts < 80) {
        vTaskDelay(pdMS_TO_TICKS(100));
        attempts++;
    }
    return wifi_connected ? ESP_OK : ESP_FAIL;
}

esp_err_t start_mqtt(void) {
    char uri[128];
    // Defaulting port to 1883 if not specified cleanly
    snprintf(uri, sizeof(uri), "mqtt://%s:1883", mqtt_broker);

    esp_mqtt_client_config_t mqtt_cfg = { .broker.address.uri = uri };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(client);
}

//=============================================================================
// Main Application
//=============================================================================
void app_main(void) {
    // 1. Initialize Flash & System
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    esp_netif_init();
    esp_event_loop_create_default();
    
    // 2. Initialize UART
    uart_config_t uart_config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(UART_PORT_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);

    // 3. Initialize Wi-Fi Stack (Once)
    wifi_stack_init();

    // 4. Boot Menu (Selection Loop)
    bool config_ready = false;
    char choice = 0;

    while (!config_ready) {
        printf("\n===================================\n");
        printf("   BOOT MENU\n");
        printf("   [O] Auto Connect (Load NVS)\n");
        printf("   [N] New Setup (Manual Entry)\n");
        printf("===================================\n");
        printf("Select >> ");
        
        // Wait for input
        while (uart_read_bytes(UART_PORT_NUM, (uint8_t*)&choice, 1, pdMS_TO_TICKS(100)) <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        printf("%c\n", choice);

        if (choice == 'O' || choice == 'o') {
            // --- Auto Mode ---
            printf("Loading configuration from NVS...\n");
            if (load_from_nvs("ssid", ssid, sizeof(ssid)) == ESP_OK &&
                load_from_nvs("pass", wifi_pass, sizeof(wifi_pass)) == ESP_OK &&
                load_from_nvs("broker", mqtt_broker, sizeof(mqtt_broker)) == ESP_OK &&
                load_from_nvs("topic", mqtt_topic, sizeof(mqtt_topic)) == ESP_OK) {
                
                printf("Credentials found for SSID: %s\nConnecting...\n", ssid);
                if (attempt_wifi_connect() == ESP_OK) {
                    config_ready = true;
                } else {
                    printf("Failed to connect! Please use New Setup [N].\n");
                }
            } else {
                printf("No saved configuration found! Please use New Setup [N].\n");
            }
        } 
        else if (choice == 'N' || choice == 'n') {
            // --- Wizard Mode ---
            printf("\n--- STARTING WIZARD ---\n");
            
            // Wi-Fi Entry
            while(1) {
                read_input("Enter SSID: ", ssid, sizeof(ssid), false);
                read_input("Enter Password: ", wifi_pass, sizeof(wifi_pass), true);
                
                printf("Attempting connection...\n");
                if (attempt_wifi_connect() == ESP_OK) {
                    printf("Wi-Fi Connected! Saving to NVS...\n");
                    save_to_nvs("ssid", ssid);
                    save_to_nvs("pass", wifi_pass);
                    break;
                } else {
                    printf("Connection Failed. Try again.\n");
                    esp_wifi_stop(); // Reset for next attempt
                }
            }

            // MQTT Entry
            read_input("Enter MQTT Broker IP: ", mqtt_broker, sizeof(mqtt_broker), false);
            read_input("Enter MQTT Topic: ", mqtt_topic, sizeof(mqtt_topic), false);
            
            save_to_nvs("broker", mqtt_broker);
            save_to_nvs("topic", mqtt_topic);
            config_ready = true;
        } 
        else {
            printf("Invalid selection.\n");
        }
    }

    // 5. Start MQTT
    start_mqtt();

    // 6. Main Publish Loop
    printf("\n--- SYSTEM RUNNING ---\n");
    ESP_LOGI(TAG, "Starting loop. Sending data to topic: %s", mqtt_topic);

    while (1) {
        if (wifi_connected && mqtt_connected) {
            int val = esp_random() % 100;
            char payload[16];
            snprintf(payload, sizeof(payload), "%d", val);
            
            esp_mqtt_client_publish(client, mqtt_topic, payload, 0, 1, 0);
            ESP_LOGI(TAG, "Published: %s", payload);
        } else {
            if (!wifi_connected) {
                ESP_LOGW(TAG, "Wi-Fi Lost. Reconnecting...");
                esp_wifi_connect();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 Seconds
    }
}