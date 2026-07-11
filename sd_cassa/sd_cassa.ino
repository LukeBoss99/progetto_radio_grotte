/*
 * Riproduzione di un file WAV dalla scheda SD su ESP32 tramite
 * amplificatore I2S MAX98357A.
 *
 * Cablaggio SD (SPI) - identico ad arduino_sd.ino:
 *   SD CS   -> P5  (GPIO 5, modificabile con SD_CS_PIN)
 *   SD SCK  -> P18 (GPIO 18)
 *   SD MISO -> P19 (GPIO 19)
 *   SD MOSI -> P23 (GPIO 23)
 *   SD VCC  -> 5V (VIN)
 *   SD GND  -> GND
 *
 * Cablaggio MAX98357A (I2S):
 *   BCLK -> P26 (GPIO 26, modificabile con I2S_BCLK_PIN)
 *   LRC  -> P25 (GPIO 25, modificabile con I2S_LRC_PIN)
 *   DIN  -> P22 (GPIO 22, modificabile con I2S_DOUT_PIN)
 *   GAIN -> non collegato (guadagno 9dB); a GND/VIN per altri guadagni
 *   SD   -> non collegato (amplificatore sempre attivo)
 *   VIN  -> 5V
 *   GND  -> GND
 *
 * Il file audio deve essere un WAV PCM non compresso, mono o stereo,
 * a 8 o 16 bit per campione. Se AUDIO_FILE non viene trovato sulla SD,
 * lo sketch cerca automaticamente il primo file .wav presente.
 */

#include <SPI.h>
#include <SD.h>
#include <driver/i2s.h>

// Pin CS (Chip Select) del modulo SD: modifica in base al tuo collegamento
#define SD_CS_PIN 5

// Pin I2S verso il MAX98357A
#define I2S_BCLK_PIN 26
#define I2S_LRC_PIN  25
#define I2S_DOUT_PIN 22

// Nome del file audio da riprodurre (usato se presente sulla SD)
#define AUDIO_FILE "/audio.wav"

struct WavInfo {
  uint32_t sampleRate;
  uint16_t numChannels;
  uint16_t bitsPerSample;
  uint32_t dataSize;
};

// Cerca il primo file .wav nella radice della scheda SD
String findFirstWavFile() {
  File root = SD.open("/");
  if (!root) {
    return "";
  }

  File entry = root.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".wav")) {
        String path = entry.path();
        entry.close();
        root.close();
        return path;
      }
    }
    entry = root.openNextFile();
  }

  root.close();
  return "";
}

// Analizza l'header di un file WAV (RIFF/WAVE) e posiziona il file
// all'inizio dei dati audio (chunk "data")
bool parseWavHeader(File &file, WavInfo &info) {
  char id[4];

  file.seek(0);
  if (file.read((uint8_t *)id, 4) != 4 || strncmp(id, "RIFF", 4) != 0) {
    return false;
  }
  file.seek(8);
  if (file.read((uint8_t *)id, 4) != 4 || strncmp(id, "WAVE", 4) != 0) {
    return false;
  }

  bool fmtFound = false;
  bool dataFound = false;

  while (file.available()) {
    if (file.read((uint8_t *)id, 4) != 4) break;

    uint32_t chunkSize;
    if (file.read((uint8_t *)&chunkSize, 4) != 4) break;

    if (strncmp(id, "fmt ", 4) == 0) {
      uint16_t audioFormat;
      file.read((uint8_t *)&audioFormat, 2);
      file.read((uint8_t *)&info.numChannels, 2);
      file.read((uint8_t *)&info.sampleRate, 4);
      file.seek(file.position() + 6); // salta byteRate (4) e blockAlign (2)
      file.read((uint8_t *)&info.bitsPerSample, 2);
      if (chunkSize > 16) {
        file.seek(file.position() + (chunkSize - 16));
      }
      fmtFound = true;
    } else if (strncmp(id, "data", 4) == 0) {
      info.dataSize = chunkSize;
      dataFound = true;
      break; // il file è ora posizionato all'inizio dei dati audio
    } else {
      file.seek(file.position() + chunkSize + (chunkSize % 2));
    }
  }

  return fmtFound && dataFound;
}

void playWav(File &file, const WavInfo &info) {
  i2s_channel_fmt_t channelFormat =
      (info.numChannels == 1) ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_RIGHT_LEFT;

  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = info.sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = channelFormat,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pinConfig);

  uint8_t buffer[1024];
  static int16_t convBuffer[1024]; // usato solo per conversione da 8 a 16 bit
  uint32_t bytesRemaining = info.dataSize;

  Serial.println("Riproduzione in corso...");

  while (bytesRemaining > 0 && file.available()) {
    size_t toRead = (bytesRemaining < sizeof(buffer)) ? bytesRemaining : sizeof(buffer);
    size_t bytesRead = file.read(buffer, toRead);
    if (bytesRead == 0) break;

    size_t bytesWritten;
    if (info.bitsPerSample == 8) {
      for (size_t i = 0; i < bytesRead; i++) {
        convBuffer[i] = ((int16_t)buffer[i] - 128) << 8;
      }
      i2s_write(I2S_NUM_0, convBuffer, bytesRead * 2, &bytesWritten, portMAX_DELAY);
    } else {
      i2s_write(I2S_NUM_0, buffer, bytesRead, &bytesWritten, portMAX_DELAY);
    }

    bytesRemaining -= bytesRead;
  }

  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("Riproduzione terminata.");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println();
  Serial.println("Inizializzazione scheda SD...");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Errore: impossibile inizializzare la scheda SD.");
    Serial.println("Verifica i collegamenti e il pin CS.");
    return;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println("Nessuna scheda SD rilevata.");
    return;
  }

  String path = AUDIO_FILE;
  if (!SD.exists(path)) {
    Serial.printf("File %s non trovato, cerco il primo file .wav sulla SD...\n", path.c_str());
    path = findFirstWavFile();
    if (path == "") {
      Serial.println("Nessun file .wav trovato sulla scheda SD.");
      return;
    }
  }

  Serial.printf("Apertura file: %s\n", path.c_str());
  File audioFile = SD.open(path);
  if (!audioFile) {
    Serial.println("Errore: impossibile aprire il file audio.");
    return;
  }

  WavInfo info;
  if (!parseWavHeader(audioFile, info)) {
    Serial.println("Errore: file WAV non valido o non riconosciuto.");
    audioFile.close();
    return;
  }

  if (info.bitsPerSample != 8 && info.bitsPerSample != 16) {
    Serial.printf("Formato non supportato: %d bit per campione (supportati 8 o 16 bit)\n", info.bitsPerSample);
    audioFile.close();
    return;
  }

  if (info.numChannels != 1 && info.numChannels != 2) {
    Serial.printf("Numero di canali non supportato: %d\n", info.numChannels);
    audioFile.close();
    return;
  }

  Serial.printf("WAV: %lu Hz, %d canali, %d bit, %lu byte di dati\n",
                info.sampleRate, info.numChannels, info.bitsPerSample, info.dataSize);

  playWav(audioFile, info);
  audioFile.close();
}

void loop() {
  // Nulla da fare qui
}
