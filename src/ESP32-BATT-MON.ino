// ===================================================================================
// CHANGELOG:
// 2.0.0 - WT, 9 CZE 2026 22:00:00 UTC - Zamrożenie stabilnej bazy kodu. Zabezpieczenie
//                                       magistral I2C/SPI przed zwisami przy braku
//                                       fizycznych modułów (EEPROM, SD).
// ===================================================================================
#define SW              "2.0.0"
#define CREATION_DATE   "WT, 9 CZE 2026 22:00:00 UTC"

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SparkFun_External_EEPROM.h>
#include <RTClib.h>
#include <INA226_WE.h>
#include <TFT_eSPI.h>        

TFT_eSPI tft = TFT_eSPI();   

#define INA_AKU1_ADDR 0x40
#define INA_AKU2_ADDR 0x41
INA226_WE ina226_1 = INA226_WE(INA_AKU1_ADDR);
INA226_WE ina226_2 = INA226_WE(INA_AKU2_ADDR);

RTC_DS3231 rtc;
ExternalEEPROM eeprom;

const int pinBuzzer = 26; 
const int pinSD_CS = 5;   
const int pinSDA = 27;    
const int pinSCL = 22;    

const float progRozbalansowania = 0.30; 
const unsigned long interwalLogowania = 5 * 60 * 1000;      
const long interwalEkranu = 4000;                          

unsigned long poprzedniMillisLog = 0;
unsigned long poprzedniMillisEkran = 0;
unsigned long poprzedniMillisBuzzer = 0;

int obecnyEkran = 0;
const int maksymalnieEkranow = 4;
bool stanAlarmu = false;
bool buzzerStan = false;

// Flagi bezpieczeństwa sprzętowego
bool eepromObecna = false;
bool sdObecna = false;

float napiecieAku1 = 0.0;
float napiecieAku2 = 0.0;
float sumaNapiec = 0.0;
float roznicaNapiec = 0.0;

struct LogStruktura {
  uint8_t typ;        
  uint32_t timestamp; 
  uint16_t u1;        
  uint16_t u2;        
  uint8_t padding;    
};

void zapiszLog(uint8_t typLogu, DateTime teraz);
void rysujEkranGraficzny(int ekran, DateTime teraz);   
void rysujKropkiGraficzne(int aktywnyEkran);
void wyslijDaneSerial(DateTime teraz);
void odczytajCalyEEPROM();
void odczytajPlikSD(String filtrDaty);
void czyscPamiecEEPROM();

void setup() {
  Serial.begin(115200);
  delay(500); 

  // --- BLOK PRZEDSTAWIANIA SIĘ SYSTEMU ---
  Serial.println("\n==================================================");
  Serial.printf(" SYSTEM MONITOROWANIA AKUMULATOROW CYD 2x12V\n");
  Serial.printf(" WERSJA OPROGRAMOWANIA (SW): %s\n", SW);
  Serial.printf(" DATA REWIZJI (CREATION):    %s\n", CREATION_DATE);
  Serial.println("==================================================");
  
  Wire.begin(pinSDA, pinSCL);

  pinMode(pinBuzzer, OUTPUT);
  noTone(pinBuzzer);

  tft.init();
  tft.setRotation(1); 
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH); 
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!rtc.begin()) Serial.println("[BLAD] Brak RTC!");
  if (!ina226_1.init() || !ina226_2.init()) Serial.println("[BLAD] Brak INA226!");
  
  ina226_1.setAverage(INA226_AVERAGE_16);
  ina226_2.setAverage(INA226_AVERAGE_16);

  if (!eeprom.begin(0x50)) {
    Serial.println("[SPRZET] EEPROM nieodnaleziony. Zapisy do EEPROM beda pomijane.");
    eepromObecna = false;
  } else {
    Serial.println("[SPRZET] Wykryto pamiec EEPROM. Aktywowano logowanie I2C.");
    eeprom.setPageSize(64);
    eeprom.setMemorySize(32768); 
    eepromObecna = true;
  }

  SPI.begin(14, 12, 13, pinSD_CS);
  if (SD.begin(pinSD_CS)) {
    if (SD.cardType() != CARD_NONE) {
      Serial.println("[SPRZET] Wykryto karte SD. Aktywowano logowanie SPI.");
      sdObecna = true;
    } else {
      Serial.println("[SPRZET] Czytnik SD pusty. Logowanie na SD wylaczone.");
      sdObecna = false;
    }
  } else {
    Serial.println("[SPRZET] Karta SD nieodnaleziona. Logowanie na SD wylaczone.");
    sdObecna = false;
  }

  tft.invertDisplay(true); 
  tft.fillScreen(TFT_BLACK); 
  Serial.println("[OK] System gotowy, start petli glownej.");
}

void loop() {
  unsigned long obecnyMillis = millis();

  // --- OGRANICZENIE ODCZYTU I2C ---
  static unsigned long ostatniOdczytI2C = 0;
  static DateTime teraz;
  
  if (obecnyMillis - ostatniOdczytI2C >= 500 || ostatniOdczytI2C == 0) {
    ostatniOdczytI2C = obecnyMillis;
    Wire.setTimeOut(50); 

    ina226_1.readAndClearFlags();
    ina226_2.readAndClearFlags();
    napiecieAku1 = ina226_1.getBusVoltage_V();
    napiecieAku2 = ina226_2.getBusVoltage_V();
    sumaNapiec = napiecieAku1 + napiecieAku2;
    roznicaNapiec = abs(napiecieAku1 - napiecieAku2);

    teraz = rtc.now();
  }

  // --- LOGIKA ALARMU ---
  if (roznicaNapiec >= progRozbalansowania) {
    if (!stanAlarmu) { 
      stanAlarmu = true;
      zapiszLog(1, teraz); 
      tft.fillScreen(TFT_BLACK); 
    }
  } else {
    if (stanAlarmu) {
      stanAlarmu = false;
      noTone(pinBuzzer); 
      tft.fillScreen(TFT_BLACK); 
    }
  }

  // --- OBSŁUGA BUZZERA ---
  if (stanAlarmu && (obecnyMillis - poprzedniMillisBuzzer >= 400)) {
    poprzedniMillisBuzzer = obecnyMillis;
    buzzerStan = !buzzerStan;
    if (buzzerStan) tone(pinBuzzer, 2000); 
    else noTone(pinBuzzer);
  }

  // --- LOGOWANIE OKRESOWE ---
  if (!stanAlarmu && (obecnyMillis - poprzedniMillisLog >= interwalLogowania)) {
    poprzedniMillisLog = obecnyMillis;
    zapiszLog(0, teraz); 
  }

  // --- OBSŁUGA KOMEND SERIAL ---
  if (Serial.available() > 0) {
    String komenda = Serial.readStringUntil('\n');
    komenda.trim();
    if (komenda.length() > 0) {
      char cmdChar = komenda.charAt(0);
      if (cmdChar == 'r' || cmdChar == 'R') wyslijDaneSerial(teraz);
      else if (cmdChar == 'l' || cmdChar == 'L') odczytajCalyEEPROM();
      else if (cmdChar == 'c' || cmdChar == 'C') czyscPamiecEEPROM();
      else if (cmdChar == 's' || cmdChar == 'S') {
        if (komenda.length() == 1) odczytajPlikSD("");
        else odczytajPlikSD(komenda.substring(1));
      }
    }
  }

  // --- ZMIANA EKRANÓW (CO 4 SEKUNDY) ---
  if (obecnyMillis - poprzedniMillisEkran >= interwalEkranu) {
    poprzedniMillisEkran = obecnyMillis;
    obecnyEkran = (obecnyEkran + 1) % maksymalnieEkranow;
    
    tft.fillScreen(TFT_BLACK); 
    rysujEkranGraficzny(obecnyEkran, teraz); 
    rysujKropkiGraficzne(obecnyEkran);
  }

  // --- INTELIGENTNE ODŚWIEŻANIE EKRANU (CO 1 SEKUNDĘ) ---
  static unsigned long ostatnieOdswiezenieTekstu = 0;
  if (obecnyMillis - ostatnieOdswiezenieTekstu >= 1000 || stanAlarmu) {
    ostatnieOdswiezenieTekstu = obecnyMillis;
    
    rysujEkranGraficzny(obecnyEkran, teraz);
    rysujKropkiGraficzne(obecnyEkran);
  }

  delay(10); 
}

void zapiszLog(uint8_t typLogu, DateTime teraz) {
  char liniaTekstu[128]; 

  if (typLogu == 0) { 
    snprintf(liniaTekstu, sizeof(liniaTekstu), "%04d-%02d-%02d %02d:%02d:%02d U1:%.2f V U2:%.2f V", 
             teraz.year(), teraz.month(), teraz.day(), teraz.hour(), teraz.minute(), teraz.second(), napiecieAku1, napiecieAku2);
  } else { 
    snprintf(liniaTekstu, sizeof(liniaTekstu), "%04d-%02d-%02d %02d:%02d:%02d Zbyt duza rozbieznosc napiec U1 i U2:%.2f V", 
             teraz.year(), teraz.month(), teraz.day(), teraz.hour(), teraz.minute(), teraz.second(), roznicaNapiec);
  }

  Serial.printf("[LOG SERIAL]: %s\n", liniaTekstu);

  if (eepromObecna) {
    uint16_t aktualnyAdres;
    eeprom.read(0, (uint8_t*)&aktualnyAdres, 2); 
    if (aktualnyAdres < 2 || aktualnyAdres > 32750) aktualnyAdres = 2; 
    
    LogStruktura nowyWpis;
    nowyWpis.typ = typLogu;
    nowyWpis.timestamp = teraz.unixtime();
    if (typLogu == 0) {
      nowyWpis.u1 = (uint16_t)(napiecieAku1 * 100);
      nowyWpis.u2 = (uint16_t)(napiecieAku2 * 100);
    } else {
      nowyWpis.u1 = (uint16_t)(napiecieAku1 * 100);
      nowyWpis.u2 = (uint16_t)(roznicaNapiec * 100);
    }

    eeprom.write(aktualnyAdres, (uint8_t*)&nowyWpis, sizeof(LogStruktura));
    aktualnyAdres += sizeof(LogStruktura);
    if (aktualnyAdres > 32750) aktualnyAdres = 2; 
    eeprom.write(0, (uint8_t*)&aktualnyAdres, 2); 
    Serial.println("[LOG EEPROM]: Zapisano.");
  }

  if (sdObecna) {
    tft.endWrite(); 
    File plikLog = SD.open("/log.txt", FILE_APPEND);
    if (plikLog) { 
      plikLog.println(liniaTekstu); 
      plikLog.close(); 
      Serial.println("[LOG SD]: Zapisano.");
    }
    tft.startWrite(); 
  }
}

void rysujEkranGraficzny(int ekran, DateTime teraz) {
  char bufor[32]; 

  if (stanAlarmu) {
    tft.setFreeFont(&FreeSansBold12pt7b); 
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("!!! ALARM !!!", 160, 25, 1); 
    
    tft.setFreeFont(&FreeSansBold9pt7b);  
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Zbyt duza asymetria:", 160, 75, 1); 
    
    snprintf(bufor, sizeof(bufor), "ROZNICA: %.2f V", roznicaNapiec);
    tft.drawCentreString(bufor, 160, 130, 1); 
    return;
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  switch(ekran) {
    case 0:
      tft.setFreeFont(&FreeSansBold12pt7b); 
      tft.drawCentreString("--- AKUMULATORY 12V ---", 160, 25, 1); 
      
      tft.setFreeFont(&FreeSansBold9pt7b);  
      snprintf(bufor, sizeof(bufor), "Bateria 1:  %.2f V", napiecieAku1); 
      tft.drawString(bufor, 40, 75); 
      
      snprintf(bufor, sizeof(bufor), "Bateria 2:  %.2f V", napiecieAku2); 
      tft.drawString(bufor, 40, 125);
      break;
      
    case 1:
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.drawCentreString("--- STATUS SYSTEMU ---", 160, 25, 1); 
      
      tft.setFreeFont(&FreeSansBold9pt7b);
      snprintf(bufor, sizeof(bufor), "Suma pakietu: %.2f V", sumaNapiec); 
      tft.drawString(bufor, 30, 75);
      
      snprintf(bufor, sizeof(bufor), "Rozbalans:     %.2f V", roznicaNapiec); 
      tft.drawString(bufor, 30, 125);
      break;
      
    case 2:
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.drawCentreString("--- CZAS SYSTEMOWY ---", 160, 25, 1); 
      
      tft.setFreeFont(&FreeSansBold9pt7b);
      snprintf(bufor, sizeof(bufor), "Godzina:  %02d:%02d:%02d", teraz.hour(), teraz.minute(), teraz.second()); 
      tft.drawString(bufor, 35, 75);
      
      snprintf(bufor, sizeof(bufor), "Data:      %02d.%02d.%04d", teraz.day(), teraz.month(), teraz.year()); 
      tft.drawString(bufor, 35, 125);
      break;

    case 3: 
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.drawCentreString("--- UPTIME SYSTEMU ---", 160, 25, 1);
      
      tft.setFreeFont(&FreeSansBold9pt7b);
      
      unsigned long sekundyCalkowite = millis() / 1000;
      unsigned long dni = sekundyCalkowite / 86400;
      unsigned long resztaDni = sekundyCalkowite % 86400;
      unsigned long godziny = resztaDni / 3600;
      unsigned long resztaGodzin = resztaDni % 3600;
      unsigned long minuty = resztaGodzin / 60;
      unsigned long sekundy = resztaGodzin % 60;

      snprintf(bufor, sizeof(bufor), "Ilosc dni:  %lu", dni);
      tft.drawString(bufor, 40, 75);
      
      snprintf(bufor, sizeof(bufor), "Czas pracy:  %02lu:%02lu:%02lu", godziny, minuty, sekundy);
      tft.drawString(bufor, 40, 125);
      break;
  }
}

void rysujKropkiGraficzne(int aktywnyEkran) {
  int startX = 130; 
  int Y = 210;       
  
  for (int i = 0; i < maksymalnieEkranow; i++) {
    if (i == aktywnyEkran) {
      tft.fillCircle(startX + (i * 30), Y, 6, TFT_GREEN); 
    } else {
      tft.fillCircle(startX + (i * 30), Y, 6, TFT_BLACK); 
      tft.drawCircle(startX + (i * 30), Y, 6, TFT_GREEN); 
    }
  }
}

void odczytajCalyEEPROM() {
  if(!eepromObecna) { Serial.println("[INFO] Brak podlaczonej pamieci EEPROM."); return; }
  uint16_t koniecAdresu; eeprom.read(0, (uint8_t*)&koniecAdresu, 2);
  if (koniecAdresu < 2 || koniecAdresu > 32750) { Serial.println("Brak logow EEPROM."); return; }
  Serial.println("\n--- LOGI EEPROM ---");
  
  for (uint16_t adr = 2; adr < koniecAdresu; adr += sizeof(LogStruktura)) {
    LogStruktura wpis; 
    eeprom.read(adr, (uint8_t*)&wpis, sizeof(LogStruktura));
    DateTime dt(wpis.timestamp); 
    float v1 = wpis.u1 / 100.0;
    float v2 = wpis.u2 / 100.0;
    
    if (wpis.typ == 0) {
      Serial.printf("%04d-%02d-%02d %02d:%02d:%02d U1:%.2f V U2:%.2f V\n", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(), v1, v2);
    } else if (wpis.typ == 1) {
      Serial.printf("%04d-%02d-%02d %02d:%02d:%02d Zbyt duza rozbieznosc:%.2f V\n", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(), v2);
    }
  }
}

void odczytajPlikSD(String filtrDaty) {
  if (!sdObecna) { Serial.println("[INFO] Brak zaladowanej karty SD."); return; }
  tft.endWrite();
  if (!SD.exists("/log.txt")) { tft.startWrite(); return; }
  File plikLog = SD.open("/log.txt", FILE_READ);
  if (plikLog) {
    while (plikLog.available()) {
      String linia = plikLog.readStringUntil('\n');
      if (filtrDaty == "" || linia.startsWith(filtrDaty)) Serial.println(linia);
    }
    plikLog.close();
  }
  tft.startWrite();
}
  
void czyscPamiecEEPROM() {
  if(!eepromObecna) { Serial.println("[INFO] Brak pamieci EEPROM do czyszczenia."); return; }
  uint16_t r = 2;
  eeprom.write(0, (uint8_t*)&r, 2);
  Serial.println("\n[INFO] Czyszczenie zakonczone.");
}
  
void wyslijDaneSerial(DateTime teraz) {
  Serial.printf("\n=== STATUS ===\nCzas: %04d-%02d-%02d %02d:%02d:%02d\nU1: %.2fV | U2: %.2fV\n", teraz.year(), teraz.month(), teraz.day(), teraz.hour(), teraz.minute(), teraz.second(), napiecieAku1, napiecieAku2);
}