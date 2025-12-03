/* Includes ---------------------------------------------------------------- */
#include <SoundSenseV1_inferencing.h> 
#include "driver/i2s.h"

// ******* CONFIGURATION *******
#define LED_PIN 2               
#define CONFIDENCE_THRESHOLD 0.7
#define I2S_WS 15               
#define I2S_SD 32               
#define I2S_SCK 14              
// *****************************

// Edge Impulse Settings
#define I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 16000 

// Buffers and Variables
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; 

// *** TIMER VARIABLES ***
unsigned long last_detection_time = 0;
const unsigned long KEEP_ALIVE_TIME = 2000; // Keep LED on for 2 seconds
// ****************************

// Global Audio Buffer
int16_t *inference_buffer; 

// Structure for Edge Impulse
typedef struct {
    int16_t *buffer;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;

// Forward declarations
bool microphone_inference_record(void);
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);
void i2s_install();
void i2s_setpin();

void setup()
{
    Serial.begin(115200);
    while (!Serial);

    // LED Setup
    pinMode(LED_PIN, OUTPUT);
    
    // --- STARTUP TEST ---
    // Use this to confirm wiring is good. LED should blink 3 times.
    Serial.println("TESTing LED Hardware...");
    for(int i=0; i<3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
    Serial.println("Hardware Test Complete.");
    // --------------------

    Serial.println("Edge Impulse Audio Classifier - FINAL DEPLOYMENT");

    inference_buffer = (int16_t *)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
    if (!inference_buffer) {
        Serial.println("ERR: Failed to allocate memory!");
        while(1); 
    }

    i2s_install();
    i2s_setpin();
    i2s_start(I2S_PORT);
}

void loop()
{
    if (microphone_inference_record() == false) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    float faucet_score = 0.0;

    // Print predictions
    ei_printf("Predictions: ");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: %.5f", result.classification[ix].label, result.classification[ix].value);
        
        // CHECK YOUR LABEL SPELLING HERE IF IT FAILS
        if (strcmp(result.classification[ix].label, "faucet-ON") == 0) {
            faucet_score = result.classification[ix].value;
        }
        // Fallback check in case you named it differently in the new model
        else if (strcmp(result.classification[ix].label, "faucet_on") == 0) {
            faucet_score = result.classification[ix].value;
        }
    }
    ei_printf("\n");

    // ******* LED LOGIC *******
    unsigned long current_time = millis();

    // 1. If we hear the faucet
    if (faucet_score > CONFIDENCE_THRESHOLD) {
        digitalWrite(LED_PIN, HIGH);
        last_detection_time = current_time; // Reset the timer
        ei_printf("--> 🚰 FAUCET DETECTED! LED ON <--\n");
    }
    
    // 2. Keep LED on for 2 seconds (Latching), then turn off
    else if (current_time - last_detection_time > KEEP_ALIVE_TIME) {
        digitalWrite(LED_PIN, LOW);
    }
    // *****************************
}

// --- Drivers ---
void i2s_install() {
    const i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = i2s_bits_per_sample_t(16),
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
        .data_in_num = I2S_SD
    };
    i2s_set_pin(I2S_PORT, &pin_config);
}

bool microphone_inference_record(void)
{
    size_t bytesIn = 0;
    for (int i=0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i+= (sample_buffer_size/2)) {
        esp_err_t result = i2s_read(I2S_PORT, &sampleBuffer, sample_buffer_size, &bytesIn, portMAX_DELAY);
        for (int j=0; j < (bytesIn/2); j++) {
             if ((i + j) < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
                 inference_buffer[i + j] = sampleBuffer[j];
             }
        }
    }
    inference.buffer = inference_buffer;
    inference.buf_count = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    inference.buf_ready = 1;
    return true;
}

int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);
    return 0;
}