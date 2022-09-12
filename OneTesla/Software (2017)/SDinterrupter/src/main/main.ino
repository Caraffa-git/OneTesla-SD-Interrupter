/*
 * ATMega328 SD card interrupter
 * Copyright 2015 oneTesla, LLC
 * See README.txt for licensing information
 */
 
#include "constants.h"
#include "data.h"
#include "datatypes.h"
#include "sdsource.h"
#include "serialsource.h"
#include "player.h"
#include "system.h"
#include "util.h"
#include "timers.h"
#include "lcd.h"

#include <LiquidCrystal.h>
#include <SdFat.h>
#include <SPI.h>

LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

note *note1, *note2;
midiMsg *last_message;
serialsource *serial;
sdsource *sd;
char volindex, menuindex = 2;
int ffreq = 20;

unsigned int bfreq = 20;
unsigned int boff = 100;
unsigned int bon = 1;

void fixedLoop();
void burstLoop();
void displayMenu();

void setup() {
  lcd_init();
  setupTimers();
  setupPins();
  player_init();
  sdsource_init();
  serialsource_init();
  displayMenu();
}

void loop() {
  unsigned char key = get_key();
  if (key == btnDOWN) {
    if (menuindex == 3) {
      menuindex = 0;
    } else {
      menuindex++;
    }
    displayMenu();
    delay(180);
  }
  if (key == btnUP) {
    if (menuindex == 0) {
      menuindex = 2;
    } else {
      menuindex--;
    }
    displayMenu();
    delay(180);
  }
  if (key == btnSELECT) {
    if (menuindex == MENU_FIXED) {
      delay(150);
      fixedLoop();
      delay(300);
    } else if (menuindex == MENU_LIVE) {
      delay(150);
      lcd_printhome("Live Mode ON");
      lcd_setcursor(0, 1);
      for (int i = 0; i < volindex; i++) {
        lcd_print((char) (1));
      }
      serialsource_run();
      lcd_printhome("Live Mode");
      delay(300);
    } else if (menuindex == MENU_BURST) {
      delay(150);
      burstLoop();
      delay(300);
    } else { // MENU_SDCARD
      delay(150);
      if (sd->valid) {
        sdsource_run();
      } else {
        sdsource_initcard();
      }
      delay(300);
    }
  }
}

void displayMenu()
{
  if (menuindex == MENU_SDCARD) {
    lcd_printhome("SD Card");
    lcd_setcursor(0, 1);
    if (sd->valid) {          
      lcd_print(sd->file_count);
      if (sd->file_count == 1) {
        lcd_print(" file, ");
      } else {
        lcd_print(" files, ");
      }
      lcd_print(sd->dir_count);
      if (sd->dir_count == 1) {
        lcd_print(" dir");
      } else {
        lcd_print(" dirs");
      }
    } else {
      lcd_print((char *)sd->last_error);
    }
  } else if (menuindex == MENU_LIVE) {
    lcd_printhome("Live Mode");
  } else if (menuindex == MENU_BURST) {
    lcd_printhome("Burst Mode");
  } else {
    lcd_printhome("Fixed Mode");
  }
}

void fixedLoop() {
  lcd_printhome("[Freq: ");
  lcd_print(ffreq);
  lcd_print("Hz");
  lcd_printat(15, 0, ']');
  lcd_setcursor(0, 1);
  for (int i = 0; i < volindex; i++) {
    lcd_print((char) (1));
  }
  char mode = 1;
  unsigned long elapsed = 0;
  unsigned int uinc, dinc = 1;
  note1->velocity = 127;
  note1->on_time = get_on_time(ffreq);
  setTimer1f(ffreq);
  engageISR1();
  for (;;) {
    unsigned char key = get_key();
    if (elapsed > 100000) {
      elapsed = 0;
      if (key != btnUP) uinc = 1;
      if (key != btnDOWN) dinc = 1;
      if (key == btnSELECT) {
        mode = !mode;
        if (mode) {
          lcd_printhome("[Freq: ");
          lcd_print(ffreq);
          lcd_print("Hz");
          lcd_printat(15, 0, ']');
          lcd_setcursor(0, 1);
          for (int i = 0; i < volindex; i++) {
            lcd_print((char) (1));
          }
        } else {
          lcd_printhome("Freq: ");
          lcd_print(ffreq);
          lcd_print("Hz");
          lcd_setcursor(0, 1);
          for (int i = 0; i < volindex; i++) {
            lcd.print((char) (1));
          }
          lcd_printat(0, 1, '[');
          lcd_printat(15, 1, ']');
        }
      }
      if (key == btnUP) {
        if (!mode) {
          lcd_setcursor(0, 1);
          incvol(&lcd);
          lcd_printat(0, 1, '[');
          lcd_printat(15, 1, ']');
        } else {
          ffreq += uinc;
          uinc += 5;
          if (ffreq > 1000) ffreq = 1000;
          lcd_printat(7, 0, 7, (unsigned int) ffreq);
          lcd_print("Hz");
          note1->on_time = get_on_time(ffreq);
          setTimer1f(ffreq);
        }
      }
      if (key == btnDOWN) {
        if (!mode) {
          lcd_setcursor(0, 1);
          decvol(&lcd);
          lcd_printat(0, 1, '[');
          lcd_printat(15, 1, ']');
        } else {
          ffreq -= dinc;
          dinc += 5;
          if (ffreq < 1) ffreq = 1;
          lcd_printat(7, 0, 7, (unsigned int) ffreq);
          lcd_print("Hz");
          note1->on_time = get_on_time(ffreq);
          setTimer1f(ffreq);;
        }
      }
    }
    delayMicroseconds(1000);
    elapsed += 1000;
  }
}

void burstLoop() {
  lcd_printhome("Base Freq: ");
  lcd_printat(0, 1, bfreq);
  lcd_print(" Hz");
  int param = 0;
  
  note1->velocity = 127;
  note1->on_time = get_on_time(bfreq);
  setTimer1f(bfreq);
  engageISR1();
  
  unsigned char state = 1;
  
  unsigned long last_t_k = millis();
  unsigned long last_t_n = millis();
  for (;;) {
    unsigned long dt_k = millis() - last_t_k;
    if (dt_k > 150) last_t_k = millis();
    unsigned char key = btnNONE;
    if (dt_k > 150) key = get_key();
    
    unsigned long dt_n = millis() - last_t_n;
    unsigned long cap = state ? bon : boff;
    if (dt_n > cap) {
      if (state) {
        disengageISR1();
      } else {
        engageISR1();
      }
      state = !state;
      last_t_n = millis();
    }
    if (key == btnSELECT) {
      param++;
      if (param == 4) param = 0;
      switch (param) {
      case 0:
        lcd_printhome("Base Freq: ");
        lcd_printat(0, 1, bfreq);
        lcd_print(" Hz");
        break;
      case 1:
        lcd_printhome("Base Pwr: ");
        lcd_setcursor(0, 1);
        for (int i = 0; i < volindex; i++) {
          lcd_print((char) (1));
        }
        break;
      case 2:
        lcd_printhome("Env ON:");
        lcd_printat(0, 1, bon);
        lcd_print(" mS");
        break;
      case 3:
        lcd_printhome("Env OFF:");
        lcd_printat(0, 1, boff);
        lcd_print(" mS");
        break;
      default:
        break;
      }
    } else if (key == btnUP) {
      switch (param) {
      case 0:
        bfreq += 50;
        if (bfreq > 1000) bfreq = 1000;
        setTimer1f(bfreq);
        lcd_clear();
        lcd_printhome("Base Freq: ");
        lcd_printat(0, 1, bfreq);
        lcd_print(" Hz");
        break;
      case 1:
        incvol(&lcd);
        break;
      case 2:
        bon++;
        if (bon > 500) bon = 500;
        lcd_clear();
        lcd_printhome("Env ON:");
        lcd_printat(0, 1, bon);
        lcd_print(" mS");
        break;
      case 3:
        boff += 10;
        lcd_clear();
        lcd_printhome("Env OFF:");
        lcd_printat(0, 1, boff);
        lcd_print(" mS");
        break;
      default:
        break;
      }
    } else if (key == btnDOWN) {
      switch (param) {
      case 0:
        if (bfreq > 49) bfreq -= 50;
        setTimer1f(bfreq);
        lcd_clear();
        lcd_printhome("Base Freq: ");
        lcd_printat(0, 1, bfreq);
        lcd_print(" Hz");
        break;
      case 1:
        decvol(&lcd);
        break;
      case 2:
        if (bon > 0) bon--;
        lcd_clear();
        lcd_printhome("Env ON:");
        lcd_printat(0, 1, bon);
        lcd_print(" mS");
        break;
      case 3:
        if (boff > 9) boff -= 10;
        lcd_clear();
        lcd_printhome("Env OFF:");
        lcd_printat(0, 1, boff);
        lcd_print(" mS");
        break;
      default:
        break;
      }
    }
  }
}

int main() {
  init();
  setup();
  for(;;) {
    loop();
  }
  return 0;
}

