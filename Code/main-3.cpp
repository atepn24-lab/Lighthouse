#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include <driver/i2s.h>


// ================= WIFI =================

const char* ssid = "ESP32_MIC";
const char* password = "12345678";

const char* serverURL = "http://192.168.4.2:2000/data";


// ================= I2S =================

#define I2S_WS   25
#define I2S_SCK  26
#define I2S_SD   33

#define I2S_PORT I2S_NUM_0


#define SAMPLE_COUNT 1024

int32_t samples[SAMPLE_COUNT];


// ================= I2S SETUP =================

void setupI2S()
{

    i2s_config_t config = {

        .mode = (i2s_mode_t)(
            I2S_MODE_MASTER |
            I2S_MODE_RX
        ),

        .sample_rate = 48000,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_32BIT,

        .channel_format =
            I2S_CHANNEL_FMT_ONLY_RIGHT,

        .communication_format =
    I2S_COMM_FORMAT_STAND_I2S,

        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,

        .dma_buf_count = 16,

        .dma_buf_len = 256,

        .use_apll = false,

        .tx_desc_auto_clear = false,

        .fixed_mclk = 0
    };


    i2s_pin_config_t pins = {

        .bck_io_num = I2S_SCK,

        .ws_io_num = I2S_WS,

        .data_out_num =
            I2S_PIN_NO_CHANGE,

        .data_in_num =
            I2S_SD
    };


    i2s_driver_install(
        I2S_PORT,
        &config,
        0,
        NULL
    );


    i2s_set_pin(
        I2S_PORT,
        &pins
    );

}


// ================= WIFI =================

void setupWiFi()
{
    WiFi.mode(WIFI_AP);

    bool ok = WiFi.softAP(ssid, password);

    if (!ok)
    {
        Serial.println("Failed to start AP");
        while (true);
    }

    Serial.println("Access Point Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
}



// ================= SEND DATA =================

void sendData(
    int amplitude,
    int peak
)
{

      Serial.println("Inside sendData()");



    String json = "{";

    json+="\"samples\":[";


    for(int i=0;i<SAMPLE_COUNT;i++)
    {

        int value = samples[i] / 8192;


        json += String(value);


        if(i<SAMPLE_COUNT-1)
            json += ",";

    }


    json+="],";


    json+="\"amplitude\":";
    json+=String(amplitude);
    json+=",";


    json+="\"peak\":";
    json+=String(peak);


    json+="}";


    HTTPClient http;


    http.begin(serverURL);

    http.addHeader(
        "Content-Type",
        "application/json"
    );


    int response =
        http.POST(json);


    Serial.print("Server response: ");

    Serial.println(response);


    http.end();

}



// ================= SETUP =================

void setup()
{
    Serial.begin(115200);

    setupWiFi();

    setupI2S();

    Serial.println("Ready");
}



// ================= LOOP =================

// ================= MAIN LOOP =================

void loop()
{
    size_t bytesRead;

    // =====================================================
    // 1. READ AUDIO FROM INMP441
    // =====================================================

    i2s_read(
        I2S_PORT,
        samples,
        sizeof(samples),
        &bytesRead,
        portMAX_DELAY
    );

    // =====================================================
    // 2. REMOVE DC OFFSET
    // =====================================================

    int64_t mean = 0;

    for(int i = 0; i < SAMPLE_COUNT; i++)
        mean += samples[i];

    mean /= SAMPLE_COUNT;

    for(int i = 0; i < SAMPLE_COUNT; i++)
        samples[i] -= mean;

    // =====================================================
    // 3. HIGH-PASS FILTER
    // Removes very low-frequency drift
    // =====================================================

    static float y = 0;
    static float lastX = 0;

    const float alpha = 0.995f;

    for(int i = 0; i < SAMPLE_COUNT; i++)
    {
        float x = (float)samples[i];

        y = alpha * (y + x - lastX);

        lastX = x;

        samples[i] = (int32_t)y;
    }
  // =====================================================
// 4. ADAPTIVE NOISE GATE
// =====================================================

// Measure average absolute noise
double avgNoise = 0;

for(int i = 0; i < SAMPLE_COUNT; i++)
{
    avgNoise += abs(samples[i]);
}

avgNoise /= SAMPLE_COUNT;

// Learned background noise
static float noiseFloor = 50000.0f;

// Update only when signal is close to the current background
if(avgNoise < noiseFloor * 2.0f)
{
    noiseFloor = 0.995f * noiseFloor +
                 0.005f * avgNoise;
}

// Threshold slightly above the background
float noiseThreshold = noiseFloor * 1.8f;

// Apply the gate
for(int i = 0; i < SAMPLE_COUNT; i++)
{
    if(abs(samples[i]) < noiseThreshold)
        samples[i] = 0;
}
  // =====================================================
// 4. AUTOMATIC GAIN CONTROL (SLOW AGC)
// =====================================================

int32_t maxValue = 1;

for(int i = 0; i < SAMPLE_COUNT; i++)
{
    if(abs(samples[i]) > maxValue)
        maxValue = abs(samples[i]);
}

// Desired maximum waveform amplitude
float target = 30000.0f / maxValue;

// Slowly adjust gain
static float gain = 1.0f;

gain = 0.99f * gain + 0.01f * target;

// Apply gain
for(int i = 0; i < SAMPLE_COUNT; i++)
{
    samples[i] = (int32_t)(samples[i] * gain);
}
 
    // =====================================================
    // 5. CALCULATE RMS AMPLITUDE + PEAK
    // =====================================================

    double sum = 0;
    int peak = 0;

    for(int i = 0; i < SAMPLE_COUNT; i++)
    {
        int32_t value = samples[i];

        sum += (double)value * value;

        if(abs(value) > peak)
            peak = abs(value);
    }

    int amplitude = (int)sqrt(sum / SAMPLE_COUNT);

    // =====================================================
    // 6. SMOOTH RMS
    // Prevents amplitude from jumping rapidly
    // =====================================================

    static float smoothRMS = 0;

    smoothRMS = 0.9f * smoothRMS + 0.1f * amplitude;

    // =====================================================
    // 7. SERIAL DEBUG
    // =====================================================

    Serial.print("Raw Sample : ");
    Serial.println(samples[0]);

    Serial.print("Amplitude  : ");
    Serial.println((int)smoothRMS);

    Serial.print("Peak       : ");
    Serial.println(peak);

    Serial.println();

    // =====================================================
    // 8. SEND DATA TO FLASK SERVER
    // =====================================================

    sendData((int)smoothRMS, peak);

    // =====================================================
    // 9. UPDATE RATE
    // =====================================================

    delay(50);
}


  
