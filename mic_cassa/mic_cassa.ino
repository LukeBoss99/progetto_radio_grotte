/*
 * Riproduzione in tempo reale (loopback) dell'audio catturato da un
 * microfono I2S SPH0645 sulla cassa collegata tramite amplificatore
 * I2S MAX98357A, su ESP32.
 *
 * Vengono usate le due periferiche I2S indipendenti dell'ESP32:
 *   - I2S_NUM_0 in modalita' RX per leggere i campioni dal microfono
 *   - I2S_NUM_1 in modalita' TX per inviarli all'amplificatore/cassa
 *
 * Cablaggio microfono SPH0645 (I2S, ingresso):
 *   BCLK -> P32 (GPIO 32, modificabile con MIC_BCLK_PIN)
 *   LRCL -> P33 (GPIO 33, modificabile con MIC_LRCL_PIN)
 *   DOUT -> P34 (GPIO 34, modificabile con MIC_DIN_PIN; solo input su ESP32)
 *   SEL  -> GND (seleziona canale sinistro)
 *   VDD  -> 3.3V (NON alimentare a 5V, il modulo non lo supporta)
 *   GND  -> GND
 *
 * Cablaggio amplificatore MAX98357A (I2S, uscita) - identico a sd_cassa.ino:
 *   BCLK -> P26 (GPIO 26, modificabile con SPK_BCLK_PIN)
 *   LRC  -> P25 (GPIO 25, modificabile con SPK_LRC_PIN)
 *   DIN  -> P22 (GPIO 22, modificabile con SPK_DOUT_PIN)
 *   GAIN -> non collegato (guadagno 9dB); a GND/VIN per altri guadagni
 *   SD   -> non collegato (amplificatore sempre attivo)
 *   VIN  -> 5V
 *   GND  -> GND
 *
 * Ogni campione I2S occupa una word a 32 bit in cui i 18 bit di dati
 * dello SPH0645 sono allineati nella parte alta (i 14 bit bassi restano
 * a zero): basta uno shift aritmetico di 16 bit per ottenere un campione
 * PCM a 16 bit con segno, pronto per l'amplificatore.
 */

#include <driver/i2s.h>

// Pin I2S del microfono SPH0645 (ingresso)
#define MIC_BCLK_PIN 32
#define MIC_LRCL_PIN 33
#define MIC_DIN_PIN  34

// Pin I2S dell'amplificatore MAX98357A (uscita)
#define SPK_BCLK_PIN 26
#define SPK_LRC_PIN  25
#define SPK_DOUT_PIN 22

#define SAMPLE_RATE   16000
#define I2S_MIC_PORT  I2S_NUM_0
#define I2S_SPK_PORT  I2S_NUM_1

#define BUFFER_SAMPLES 256

static int32_t micBuffer[BUFFER_SAMPLES];
static int16_t spkBuffer[BUFFER_SAMPLES];

void setupMic() {
  i2s_config_t micConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t micPins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = MIC_BCLK_PIN,
    .ws_io_num = MIC_LRCL_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_DIN_PIN
  };

  i2s_driver_install(I2S_MIC_PORT, &micConfig, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &micPins);
}

void setupSpeaker() {
  i2s_config_t spkConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t spkPins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = SPK_BCLK_PIN,
    .ws_io_num = SPK_LRC_PIN,
    .data_out_num = SPK_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPK_PORT, &spkConfig, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &spkPins);
}

// DEBUG: genera un tono a 440Hz per verificare che il percorso verso la
// cassa (I2S_NUM_1 / MAX98357A) funzioni, senza passare dal microfono.
void playTestTone() {
  const float freq = 440.0;
  const int totalSamples = SAMPLE_RATE * 2; // 2 secondi
  static int16_t toneBuffer[BUFFER_SAMPLES];
  int samplesDone = 0;

  Serial.println("DEBUG: riproduco tono di test a 440Hz per 2 secondi...");

  while (samplesDone < totalSamples) {
    int chunk = min(BUFFER_SAMPLES, totalSamples - samplesDone);
    for (int i = 0; i < chunk; i++) {
      float t = (float)(samplesDone + i) / SAMPLE_RATE;
      toneBuffer[i] = (int16_t)(10000.0 * sinf(2.0 * PI * freq * t));
    }
    size_t bytesWritten = 0;
    i2s_write(I2S_SPK_PORT, toneBuffer, chunk * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    samplesDone += chunk;
  }

  Serial.println("DEBUG: tono di test terminato.");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("Avvio microfono I2S (SPH0645) e cassa I2S (MAX98357A)...");

  setupMic();
  setupSpeaker();

  playTestTone();

  Serial.println("Pronto: l'audio del microfono viene riprodotto sulla cassa in tempo reale.");
}

// Intervallo minimo (ms) tra due stampe del livello del segnale sul monitor seriale
#define LEVEL_PRINT_INTERVAL_MS 200

static unsigned long lastLevelPrint = 0;

void loop() {
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_MIC_PORT, micBuffer, sizeof(micBuffer), &bytesRead, portMAX_DELAY);

  if (result != ESP_OK || bytesRead == 0) {
    return;
  }

  size_t samplesRead = bytesRead / sizeof(int32_t);
  int16_t peak = 0;

  for (size_t i = 0; i < samplesRead; i++) {
    // I 18 bit di dati dello SPH0645 occupano gia' la parte alta della word
    // a 32 bit (i 14 bit bassi restano a zero): basta prendere i 16 bit piu'
    // significativi con uno shift aritmetico per ottenere un campione PCM
    // a 16 bit con segno.
    int32_t sample = micBuffer[i] >> 16;
    if (sample > INT16_MAX) sample = INT16_MAX;
    if (sample < INT16_MIN) sample = INT16_MIN;
    spkBuffer[i] = (int16_t)sample;

    int16_t absSample = (sample < 0) ? (int16_t)(-sample) : (int16_t)sample;
    if (absSample > peak) peak = absSample;
  }

  size_t bytesWritten = 0;
  i2s_write(I2S_SPK_PORT, spkBuffer, samplesRead * sizeof(int16_t), &bytesWritten, portMAX_DELAY);

  unsigned long now = millis();
  if (now - lastLevelPrint >= LEVEL_PRINT_INTERVAL_MS) {
    lastLevelPrint = now;
    int barLen = map(peak, 0, INT16_MAX, 0, 40);
    Serial.printf("Livello mic: %5d  [", peak);
    for (int i = 0; i < 40; i++) {
      Serial.print(i < barLen ? '#' : ' ');
    }
    Serial.println("]");
  }
}
