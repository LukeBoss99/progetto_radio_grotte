/*
 * Lettura scheda SD su ESP32
 * Elenca tutti i file presenti sulla scheda SD (anche nelle sottocartelle)
 *
 * Cablaggio testato e funzionante su ESP32-S NodeMCU V1.1 (bus SPI):
 *   SD CS   -> P5  (GPIO 5, modificabile con SD_CS_PIN)
 *   SD SCK  -> P18 (GPIO 18)
 *   SD MISO -> P19 (GPIO 19)
 *   SD MOSI -> P23 (GPIO 23)
 *   SD VCC  -> 5V (VIN)
 *   SD GND  -> GND
 */

#include <SPI.h>
#include <SD.h>

// Pin CS (Chip Select) del modulo SD: modifica in base al tuo collegamento
#define SD_CS_PIN 5

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Elenco cartella: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Impossibile aprire la cartella");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Non è una cartella");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  CARTELLA : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE     : ");
      Serial.print(file.name());
      Serial.print("  \tDimensione: ");
      Serial.print(file.size());
      Serial.println(" byte");
    }
    file = root.openNextFile();
  }
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

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("Nessuna scheda SD rilevata.");
    return;
  }

  Serial.print("Tipo di scheda SD: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("SCONOSCIUTO");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("Dimensione scheda SD: %lluMB\n", cardSize);

  // Elenca tutti i file, scendendo fino a 3 livelli di sottocartelle
  listDir(SD, "/", 3);

  Serial.println("Lettura completata.");
}

void loop() {
  // Nulla da fare qui
}
