#include "ScopeEngine.h"

// --- Variabile Globale ---
volatile uint16_t adc_buffer0[ADC_BUFFER_SIZE];
volatile uint16_t adc_buffer1[ADC_BUFFER_SIZE];
volatile uint8_t active_buffer_id = 0;
volatile bool adc_buffer_ready = false;
uint16_t oscilloscopeBuffer[ADC_BUFFER_SIZE];

// Buffer Raw. Chiar daca e Mono, alocam destul. 
// ADC_BUFFER_SIZE = 512. 
uint16_t i2sRawBuffer[ADC_BUFFER_SIZE]; 

// Task Handle
TaskHandle_t i2sTaskHandle;

// Configurare I2S
const i2s_port_t I2S_PORT = I2S_NUM_0;

void i2sTask(void *pvParameters) {
    size_t bytesRead = 0;
    
    while (1) {
        // Citim 512 samples (1024 bytes)
        esp_err_t result = i2s_read(I2S_PORT, (void*)i2sRawBuffer, sizeof(i2sRawBuffer), &bytesRead, portMAX_DELAY);
        
        if (result == ESP_OK && bytesRead == sizeof(i2sRawBuffer)) {
            
            volatile uint16_t* targetBuffer = (active_buffer_id == 0) ? adc_buffer0 : adc_buffer1;
            
            uint16_t lastVal = 2048; // Valoare default (mijloc)
            
            for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                uint16_t val = i2sRawBuffer[i] & 0x0FFF;
                
                // Filtru simplu pentru 0-uri (daca apar din cauza interleaving ascuns)
                if (val == 0) {
                    targetBuffer[i] = lastVal;
                } else {
                    targetBuffer[i] = val;
                    lastVal = val;
                }
            }

            active_buffer_id = !active_buffer_id; 
            adc_buffer_ready = true; 
        } else {
            vTaskDelay(1); // Anti-spin lock in caz de eroare
        }
    }
}

void scopeInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
        .sample_rate = 50000, // 50 kHz
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // Mono Safe
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 1024, // Buffer DMA generos
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_6); // GPIO 34
    i2s_adc_enable(I2S_PORT);
}

void scopeStart() {
    xTaskCreatePinnedToCore(
        i2sTask, "I2S_Reader", 4096, NULL, 1, &i2sTaskHandle, 0
    );
}

bool scopeCheckTrigger() {
    if (adc_buffer_ready) {
        uint8_t finished_buffer_id = !active_buffer_id; 
        volatile uint16_t* src = (finished_buffer_id == 0) ? adc_buffer0 : adc_buffer1;
        
        memcpy(oscilloscopeBuffer, (void*)src, ADC_BUFFER_SIZE * sizeof(uint16_t));
        adc_buffer_ready = false; 
        return true; 
    }
    return false;
}
