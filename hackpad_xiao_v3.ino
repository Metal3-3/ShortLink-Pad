/*
  HACKPAD - Seeeduino XIAO (Windows)  -  v3
  ---------------------------------------------------------------
  Donanim:
    7x keyswitch   -> D1, D2, D3, D4, D5, D6, D7  (GND'ye, INPUT_PULLUP)
    Rotary encoder -> A: D9, B: D10, Switch(SW): D11
    OLED (0.91" SSD1306, 128x32, I2C) -> SDA: D4, SCL: D5  (XIAO'nun
                                          gercek donanimsal I2C pinleri)

  ISLEVLER:
    - 7 buton  -> her biri Win+R ile kendi linkini acar, OLED'de logo/etiket gosterir
    - Encoder cevirme -> ses seviyesi yukari/asagi (Consumer Control), OLED'de
                         dolan/bosalan bir ses cubugu animasyonu
    - Encoder butonu (D11) -> medya oynat/durdur (video oynuyorsa durdurur),
                               OLED'de ucgen "durdur" ikonu gosterir

  GEREKEN KUTUPHANELER (Library Manager'dan kur):
    - Adafruit GFX Library
    - Adafruit SSD1306
    - HID-Project (NicoHood)  -> klavye + ses/medya tuslari icin bunu kullaniyoruz,
                                  standart Keyboard.h ile AYNI ANDA kullanma (cakisir).

  NOT: Ses cubugu ekranda 0-100 arasi KENDI TUTTUGUMUZ bir deger; XIAO,
  Windows'un gercek ses seviyesini okuyamaz, sadece Consumer.write ile
  "ses arttir/azalt" komutu gonderir. Ekrandaki % gosterge yaklasik/temsili'dir.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HID-Project.h>

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  32
#define OLED_RESET     -1
#define OLED_ADDR      0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- 7 buton ve linkler (kendi linklerinle degistir) ---
const int NUM_BUTTONS = 7;
const int BTN_PINS[NUM_BUTTONS] = {1, 2, 3, 4, 5, 6, 7};
const String LINKS[NUM_BUTTONS] = {
  "https://youtube.com",
  "https://google.com",
  "https://github.com",
  "https://chatgpt.com",
  "https://discord.com",
  "https://instagram.com",
  "https://mail.google.com"
};
const String LABELS[NUM_BUTTONS] = {
  "YouTube", "Google", "GitHub", "ChatGPT", "Discord", "Instagram", "Gmail"
};

// --- Rotary encoder ---
const int ENC_A  = 9;
const int ENC_B  = 10;
const int ENC_SW = 11;
volatile int lastEncoded = 0;
volatile long encoderPos = 0;

int volumeLevel = 50; // 0-100 arasi temsili deger

const int DEBOUNCE_MS = 250;
unsigned long lastPress[NUM_BUTTONS] = {0};
unsigned long lastSwPress = 0;

// 32x32 mono bitmap - play/YouTube ikonu (Adafruit_GFX drawBitmap formati)
static const unsigned char PROGMEM youtube_logo_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xF0,
  0x1F, 0xFF, 0xFF, 0xF8, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC,
  0x7F, 0xFF, 0xFF, 0xFE, 0x7F, 0xFF, 0xFF, 0xFE, 0x7F, 0xFF, 0xFF, 0xFE,
  0x7F, 0xF7, 0xFF, 0xFE, 0x7F, 0xF1, 0xFF, 0xFE, 0x7F, 0xF0, 0xFF, 0xFE,
  0x7F, 0xF0, 0x3F, 0xFE, 0x7F, 0xF0, 0x1F, 0xFE, 0x7F, 0xF0, 0x07, 0xFE,
  0x7F, 0xF0, 0x03, 0xFE, 0x7F, 0xF0, 0x00, 0xFE, 0x7F, 0xF0, 0x03, 0xFE,
  0x7F, 0xF0, 0x07, 0xFE, 0x7F, 0xF0, 0x1F, 0xFE, 0x7F, 0xF0, 0x3F, 0xFE,
  0x7F, 0xF0, 0xFF, 0xFE, 0x7F, 0xF1, 0xFF, 0xFE, 0x7F, 0xF7, 0xFF, 0xFE,
  0x7F, 0xFF, 0xFF, 0xFE, 0x7F, 0xFF, 0xFF, 0xFE, 0x3F, 0xFF, 0xFF, 0xFC,
  0x3F, 0xFF, 0xFF, 0xFC, 0x1F, 0xFF, 0xFF, 0xF8, 0x0F, 0xFF, 0xFF, 0xF0,
  0x03, 0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00,
};

void setup() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);

  BootKeyboard.begin();
  Consumer.begin();

  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  showIdleScreen();
}

void loop() {
  // 7 buton kontrolu -> link ac
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (digitalRead(BTN_PINS[i]) == LOW && millis() - lastPress[i] > DEBOUNCE_MS) {
      lastPress[i] = millis();
      if (i == 0) showYoutubeLogo();
      else showGenericLogo(LABELS[i]);
      openLink(LINKS[i]);
      delay(1500);
      showIdleScreen();
    }
  }

  // Encoder donme -> ses
  static long lastPos = 0;
  if (encoderPos != lastPos) {
    if (encoderPos > lastPos) {
      Consumer.write(MEDIA_VOLUME_UP);
      volumeLevel = min(100, volumeLevel + 5);
    } else {
      Consumer.write(MEDIA_VOLUME_DOWN);
      volumeLevel = max(0, volumeLevel - 5);
    }
    lastPos = encoderPos;
    animateVolumeBar(volumeLevel);
  }

  // Encoder switch -> oynat/durdur
  if (digitalRead(ENC_SW) == LOW && millis() - lastSwPress > DEBOUNCE_MS) {
    lastSwPress = millis();
    Consumer.write(MEDIA_PLAY_PAUSE);
    showStopIcon();
    delay(1000);
    showIdleScreen();
  }
}

// --- Encoder interrupt handler ---
void updateEncoder() {
  int MSB = digitalRead(ENC_A);
  int LSB = digitalRead(ENC_B);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
    encoderPos++;
  } else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
    encoderPos--;
  }
  lastEncoded = encoded;
}

// --- Windows: Win+R -> linki yaz -> Enter ---
void openLink(String url) {
  BootKeyboard.press(KEY_LEFT_GUI);
  BootKeyboard.press('r');
  delay(100);
  BootKeyboard.releaseAll();
  delay(300);

  BootKeyboard.print(url);
  delay(100);
  BootKeyboard.press(KEY_RETURN);
  delay(50);
  BootKeyboard.releaseAll();
}

// --- OLED ekranlari ---
void showIdleScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 12);
  display.print("Hackpad hazir");
  display.display();
}

void showYoutubeLogo() {
  display.clearDisplay();
  display.drawBitmap(0, 0, youtube_logo_bmp, 32, 32, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(40, 12);
  display.print("YouTube");
  display.display();
}

// Diger butonlar icin genel play-ikonu + etiket
void showGenericLogo(String label) {
  display.clearDisplay();

  int rectW = 32, rectH = 32;
  display.fillRoundRect(0, 0, rectW, rectH, 6, SSD1306_WHITE);

  int cx = rectW / 2;
  int cy = rectH / 2;
  display.fillTriangle(
    cx - 5, cy - 7,
    cx - 5, cy + 7,
    cx + 8, cy,
    SSD1306_BLACK
  );

  display.setTextSize(1);
  display.setCursor(40, 12);
  display.print(label);

  display.display();
}

// Ses seviyesi degisince dolan/bosalan bar animasyonu
void animateVolumeBar(int level) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Ses: ");
  display.print(level);
  display.print("%");

  int barX = 0, barY = 18, barW = 122, barH = 10;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);

  int targetFill = map(level, 0, 100, 0, barW - 2);

  // basit dolma animasyonu: adim adim ciz
  static int currentFill = 0;
  int step = (targetFill > currentFill) ? 4 : -4;
  while (currentFill != targetFill) {
    currentFill += step;
    if ((step > 0 && currentFill > targetFill) || (step < 0 && currentFill < targetFill)) {
      currentFill = targetFill;
    }
    display.fillRect(barX + 1, barY + 1, max(0, currentFill), barH - 2, SSD1306_WHITE);
    display.display();
    delay(8);
  }
}

// Encoder butonuna basinca ucgen "durdur/oynat" ikonu
void showStopIcon() {
  display.clearDisplay();

  int cx = SCREEN_WIDTH / 2 - 20;
  int cy = SCREEN_HEIGHT / 2;
  display.fillTriangle(
    cx - 10, cy - 12,
    cx - 10, cy + 12,
    cx + 12, cy,
    SSD1306_WHITE
  );

  display.setTextSize(1);
  display.setCursor(cx + 24, cy - 4);
  display.print("DURDU");

  display.display();
}
