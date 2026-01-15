# Instrukcja Kompilacji - KC868-A16 Firmware z obsługą VPC-M0701S

## Wymagania wstępne

### Sprzęt
- Płyta KC868-A16 (ESP32)
- Kabel USB do programowania
- Komputer z systemem Windows/Linux/macOS

### Oprogramowanie
- Arduino IDE 2.x lub PlatformIO
- Sterowniki USB (CH340/CP2102 w zależności od płyty)

## Metoda 1: Kompilacja z Arduino IDE (ZALECANA)

### Krok 1: Instalacja Arduino IDE

1. Pobierz Arduino IDE 2.x z oficjalnej strony:
   - https://www.arduino.cc/en/software
2. Zainstaluj Arduino IDE
3. Uruchom Arduino IDE

### Krok 2: Dodanie obsługi ESP32

1. W Arduino IDE otwórz: **File → Preferences** (Plik → Preferencje)
2. W polu "Additional Boards Manager URLs" dodaj:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Kliknij **OK**
4. Otwórz: **Tools → Board → Boards Manager** (Narzędzia → Płyta → Menadżer płyt)
5. Wyszukaj "ESP32" i zainstaluj **esp32 by Espressif Systems** (wersja 2.0.x lub nowsza)

### Krok 3: Instalacja wymaganych bibliotek

W Arduino IDE otwórz: **Tools → Manage Libraries** (Narzędzia → Zarządzaj bibliotekami)

Zainstaluj następujące biblioteki (wyszukaj po nazwie i kliknij "Install"):

1. **ModbusMaster** by 4-20ma
   - Wyszukaj: "ModbusMaster"
   - Zainstaluj najnowszą wersję

2. **ModbusIP ESP8266/ESP32** by emelianov
   - Wyszukaj: "ModbusIP"
   - Wybierz: "ModbusIP ESP8266/ESP32"
   - Zainstaluj najnowszą wersję

3. **PubSubClient** by Nick O'Leary
   - Wyszukaj: "PubSubClient"
   - Zainstaluj najnowszą wersję

4. **PCF8574** by Renzo Mischianti
   - Wyszukaj: "PCF8574"
   - Wybierz wersję od Renzo Mischianti
   - Zainstaluj najnowszą wersję

**Uwaga**: Biblioteki Arduino.h, WiFi, ETH, WebServer, Preferences, Wire są wbudowane w platformę ESP32.

### Krok 4: Sklonowanie repozytorium

Otwórz terminal/wiersz poleceń i wykonaj:

```bash
# Sklonuj repozytorium
git clone https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S.git

# Przejdź do katalogu
cd Kincony_VPC_M0701S

# Przełącz się na branch z kodem
git checkout copilot/add-vpc-m0701s-inverter-type
```

### Krok 5: Otwarcie projektu w Arduino IDE

1. W Arduino IDE kliknij: **File → Open** (Plik → Otwórz)
2. Przejdź do katalogu repozytorium
3. Otwórz plik: **Kincony_VPC_M0701S.ino**
4. Arduino IDE automatycznie otworzy wszystkie pliki .ino, .cpp i .h w zakładkach

### Krok 6: Konfiguracja płyty

1. Wybierz płytę: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
2. Ustaw parametry portu:
   - **Tools → Port** - wybierz port COM (Windows) lub /dev/ttyUSB0 (Linux)
   - **Tools → Upload Speed** - 115200 (lub wolniej jeśli są problemy)
   - **Tools → Flash Size** - 4MB (jeśli dostępne)
   - **Tools → Partition Scheme** - Default 4MB with spiffs

### Krok 7: Kompilacja (weryfikacja)

1. Kliknij przycisk **✓ Verify** (Weryfikuj) na pasku narzędzi
2. Poczekaj na zakończenie kompilacji
3. Sprawdź komunikaty w oknie Output (na dole)
4. Jeśli kompilacja się powiodła, zobaczysz komunikat:
   ```
   Sketch uses XXXXX bytes (XX%) of program storage space.
   Global variables use XXXXX bytes (XX%) of dynamic memory.
   ```

### Krok 8: Wgranie do płyty

1. Podłącz płytę KC868-A16 do komputera przez USB
2. Upewnij się, że wybrany jest właściwy port COM/USB
3. Kliknij przycisk **→ Upload** (Wgraj)
4. Poczekaj na zakończenie procesu
5. Monitor szeregowy pokaże komunikaty startowe firmware

### Krok 9: Weryfikacja działania

1. Otwórz: **Tools → Serial Monitor** (Narzędzia → Monitor szeregowy)
2. Ustaw baud rate: **115200**
3. Po restarcie płyty powinieneś zobaczyć komunikaty:
   ```
   [BOOT] KC868-A16 Multi-SID + Full WWW + VPC M0701S
   [AutoMulti v21a FIX V6c3 FULL SinglePage=ON] SIDs: 1,2,3,4,5,6
   [HTTP] Server started
   [READY]
   ```

## Metoda 2: Kompilacja z PlatformIO

### Krok 1: Instalacja PlatformIO

#### Opcja A: PlatformIO IDE (Visual Studio Code)
1. Zainstaluj Visual Studio Code: https://code.visualstudio.com/
2. W VS Code otwórz: Extensions (Ctrl+Shift+X)
3. Wyszukaj "PlatformIO IDE" i zainstaluj

#### Opcja B: PlatformIO Core (CLI)
```bash
pip install platformio
```

### Krok 2: Sklonowanie repozytorium

```bash
git clone https://github.com/horzo-kincony-reader/Kincony_VPC_M0701S.git
cd Kincony_VPC_M0701S
git checkout copilot/add-vpc-m0701s-inverter-type
```

### Krok 3: Utworzenie pliku platformio.ini

Utwórz plik `platformio.ini` w głównym katalogu projektu:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

; Ustawienia seryjne
monitor_speed = 115200
upload_speed = 921600

; Biblioteki wymagane do kompilacji
lib_deps = 
    4-20ma/ModbusMaster@^2.0.1
    emelianov/modbus-esp8266@^4.1.0
    knolleary/PubSubClient@^2.8
    xreef/PCF8574 library@^2.3.4

; Opcje budowania
build_flags = 
    -D RS485_RX_PIN=16
    -D RS485_TX_PIN=13

; Partycje (opcjonalne, dostosuj do potrzeb)
board_build.partitions = default.csv
```

### Krok 4: Kompilacja z PlatformIO

#### Z PlatformIO IDE (VS Code):
1. Otwórz folder projektu w VS Code
2. PlatformIO automatycznie wykryje `platformio.ini`
3. Kliknij ikonę PlatformIO na pasku bocznym
4. Wybierz: **Build** (kompilacja) lub **Upload** (wgranie)

#### Z PlatformIO Core (CLI):
```bash
# Kompilacja
platformio run

# Wgranie do płyty
platformio run --target upload

# Monitor szeregowy
platformio device monitor
```

## Rozwiązywanie problemów

### Problem: Biblioteka nie znaleziona

**Rozwiązanie**:
- W Arduino IDE: sprawdź, czy biblioteka jest zainstalowana (Tools → Manage Libraries)
- W PlatformIO: sprawdź sekcję `lib_deps` w `platformio.ini`

### Problem: Port COM nie wykryty

**Rozwiązanie**:
- Zainstaluj sterowniki USB (CH340 lub CP2102)
- Windows: Sprawdź w Menedżerze Urządzeń
- Linux: Dodaj użytkownika do grupy dialout: `sudo usermod -a -G dialout $USER`

### Problem: Błąd kompilacji "ESP32 board not found"

**Rozwiązanie**:
- Upewnij się, że platforma ESP32 jest zainstalowana w Boards Manager
- Zrestartuj Arduino IDE

### Problem: Błąd "A fatal error occurred: Failed to connect"

**Rozwiązanie**:
- Naciśnij i przytrzymaj przycisk BOOT na płycie podczas wgrywania
- Zmniejsz Upload Speed (np. do 115200)
- Sprawdź kabel USB (użyj kabla z danymi, nie tylko zasilającego)

### Problem: Niewystarczająca pamięć

**Rozwiązanie**:
- Wybierz odpowiednią partycję: Tools → Partition Scheme → Default 4MB with spiffs
- Usuń nieużywane biblioteki lub kod

## Konfiguracja po wgraniu

1. **Znajdź adres IP płyty**:
   - Połącz się z siecią WiFi AP o nazwie `KINCONY_WIFI` (hasło: `darol177`)
   - Lub sprawdź adres IP w sieci Ethernet

2. **Otwórz interfejs WWW**:
   - Przeglądarka: `http://[adres-IP]/`
   - Login: `admin` / Hasło: `darol177`

3. **Konfiguruj falowniki VPC**:
   - Przejdź do: `http://[adres-IP]/inverter_master/config`
   - Dla każdego SID wybierz typ: ME300 lub VPC-M0701S
   - Ustaw parametry VPC (adres, skalowanie, itp.)
   - Kliknij "Save Configuration"

4. **Zweryfikuj działanie**:
   - Sprawdź status: `http://[adres-IP]/inverter_master`
   - Monitoruj MQTT (jeśli skonfigurowany)
   - Sprawdź ModbusTCP port 502

## Dalsze kroki

- Przeczytaj **README.md** - pełna dokumentacja konfiguracji
- Przeczytaj **IMPLEMENTATION_NOTES.md** - szczegóły techniczne i testy
- Przeczytaj **PR_SUMMARY.md** - podsumowanie zmian

## Wsparcie

W razie problemów:
1. Sprawdź komunikaty w Serial Monitor (115200 baud)
2. Przeczytaj sekcję "Rozwiązywanie problemów" powyżej
3. Sprawdź logi w konsoli przeglądarki (F12) dla problemów z UI
4. Zgłoś issue na GitHub z pełnymi logami

---

**Powodzenia w kompilacji!** 🚀
