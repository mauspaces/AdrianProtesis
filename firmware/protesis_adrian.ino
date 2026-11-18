/*
  Prótesis izquierda de Adrián · HAND-E · firmware de control (ESP32-S3, p. ej. Seeed XIAO ESP32S3)
  ------------------------------------------------------------------------------------------------
  Un sensor EMG (Sichiray, electrodo seco) en el socket controla TRES motores:
    * MG995: cierra los 4 dedos por el diferencial (cada dedo se detiene al tocar);
    * MG90S del pulgar: cierra el pulgar junto con los dedos (va un poco adelantado para que la punta del índice
      llegue a la punta del pulgar) y se detiene solo si toca el objeto;
    * MG90S de la muñeca: gira la mano ±80° desde la neutra (pulgar arriba).
  MODO MANO (al encender):  mantener la contracción = cerrar (más fuerte = más rápido); relajar = sostiene;
                            una contracción CORTA = abrir. Abierta y sin usarse 3 s, se RELAJA sola (postura de reposo
                            natural: dedos un poco doblados y pulgar a medio camino); vuelve a cerrar desde ahí.
  MODO MUÑECA:              mantener la contracción = girar; una contracción corta = cambiar el sentido.
  DOBLE contracción corta = cambiar de modo (el LED parpadea 1 vez = mano, 2 = muñeca). Del modo muñeca se regresa
  solo a la mano tras 4 s sin moverse. La mano NO se abre con la doble contracción: se puede girar sosteniendo algo.
  Calibración personalizada con umbrales adaptativos: al encender, 3 s en reposo y 3 s apretando fuerte; el umbral
  se ajusta solo mientras el brazo está en reposo.
  Seguridad: límites de los servos, alivio después de agarrar (no se calientan), batería baja = abre y se detiene.

  Conexiones (XIAO ESP32S3):
    A0 (D0)  <- señal del sensor EMG (SIG)            sensor VCC <- 3.3 V, GND común
    A1 (D1)  <- divisor de batería 100 kΩ / 33 kΩ (2S: 8.4 V -> 2.08 V)
    D2       -> señal del servo de los DEDOS (MG995)   servos y XIAO: convertidor de 5 V / 5 A + capacitor 1000 µF (NO de la ESP32)
    D3       -> señal del servo del PULGAR (MG90S)
    D4       -> señal del servo de la MUÑECA (MG90S)
  Librería: ESP32Servo (Kevin Harrington), desde el Administrador de librerías de Arduino.
  Comandos por USB (115200) para la demostración: a = abrir, c = cerrar, k = recalibrar, p = mostrar la señal,
    + / - = mover solo el pulgar 5°, < / > = girar la muñeca 10°, n = muñeca a la neutra, m = cambiar de modo.
  AJUSTES (una vez): si al cerrar el pulgar se ABRE, intercambia PUL_A y PUL_C. Si la muñeca gira al revés de lo que
  esperas, cambia MUNECA_SENTIDO a -1. La neutra (pulgar arriba) es MUNECA_NEUTRA: ponla al montar el cuerno.
*/
#include <ESP32Servo.h>

const int PIN_EMG = A0, PIN_BAT = A1, PIN_MANO = D2, PIN_PULGAR = D3, PIN_MUNECA = D4, PIN_LED = LED_BUILTIN;
const float MANO_ABIERTA = 0.0, MANO_CERRADA = 80.0;          // grados del servo de los dedos (el diferencial aprieta a ~78°)
const float PUL_A = 25.0, PUL_C = 155.0;                       // grados del servo del pulgar: abierto / cerrado (pinza)
const float MANO_REPOSO = 16.0;                                // postura de reposo natural (20 % del cierre)
const unsigned long T_REPOSO = 3000;                           // ms abierta sin usarse antes de relajarse
const float ADELANTO = 0.8;                                    // el pulgar llega a cerrado cuando los dedos van al 80 %
const float MUNECA_NEUTRA = 90.0, MUNECA_LIMITE = 77.0;        // grados del servo de la muñeca: neutra y giro máximo (tope físico a 80°)
const int MUNECA_SENTIDO = 1;                                  // 1 o -1 (para que "positivo" sea pronación)
const float VEL_MANO = 120.0, VEL_MUNECA = 90.0;               // grados por segundo con contracción máxima
const float ALIVIO = 3.0, ALIVIO_PULGAR = 4.0;                 // grados que aflojan 1.5 s después de agarrar
const float BAT_AVISO = 6.6, BAT_CORTE = 6.4, DIVISOR = (100.0 + 33.0) / 33.0;
const unsigned long T_PULSO_MIN = 60, T_PULSO_MAX = 380, T_DOBLE = 600, T_REGRESO = 4000;

Servo sMano, sPulgar, sMuneca;
float base = 0, ruido = 1, maximo = 100, env = 0, umbralOn = 30, umbralOff = 18;
float angMano = MANO_ABIERTA, ajustePulgar = 0, giro = 0;      // giro: grados desde la neutra (+ = pronación)
int sentido = 1;                                               // sentido del giro en modo muñeca
bool activo = false, plot = false, aliviado = true, modoMuneca = false, pulsoPendiente = false;
unsigned long tInicio = 0, tUltimaActividad = 0, tUltimoCierre = 0, tPulso = 0, tUltimoGiro = 0, tAbierta = 0;

float leerEMG() { return analogReadMilliVolts(PIN_EMG); }
float bateria() { return analogReadMilliVolts(PIN_BAT) / 1000.0 * DIVISOR; }
void parpadear(int n, int ms) { for (int i = 0; i < n; i++) { digitalWrite(PIN_LED, LOW); delay(ms); digitalWrite(PIN_LED, HIGH); delay(ms); } }

float pulgarPara(float angDedos) {                             // el pulgar sigue a los dedos, un poco adelantado
  float f = constrain((angDedos - MANO_ABIERTA) / ((MANO_CERRADA - MANO_ABIERTA) * ADELANTO), 0.0f, 1.0f);
  return PUL_A + f * (PUL_C - PUL_A);
}
void escribirMuneca() { giro = constrain(giro, -MUNECA_LIMITE, MUNECA_LIMITE); sMuneca.write((int)round(MUNECA_NEUTRA + MUNECA_SENTIDO * giro)); }
void mover() {
  sMano.write((int)round(angMano));
  float p = pulgarPara(angMano) + ajustePulgar;
  sPulgar.write((int)round(constrain(p, min(PUL_A, PUL_C), max(PUL_A, PUL_C))));
}
void cambiarModo() {
  modoMuneca = !modoMuneca; sentido = 1; tUltimoGiro = millis();
  parpadear(modoMuneca ? 2 : 1, 120);
  Serial.println(modoMuneca ? F("MODO MUNECA") : F("MODO MANO"));
}

float envolvente(float x) {                                    // |x - base| filtrada (sirve con señal cruda o rectificada)
  static float lp = 0; lp += 0.02f * (fabs(x - base) - lp); return lp;
}

void calibrar() {
  Serial.println(F("CALIBRACION: relaja el brazo 3 s...")); parpadear(3, 150);
  double s = 0, s2 = 0; int n = 0; unsigned long t = millis();
  while (millis() - t < 3000) { float x = leerEMG(); s += x; s2 += x * x; n++; delay(1); }
  base = s / n; ruido = sqrt(max(1e-3, s2 / n - base * base));
  Serial.println(F("Ahora APRIETA fuerte 3 s...")); parpadear(1, 400);
  float lp = 0, eMax = 0; t = millis();
  while (millis() - t < 3000) { lp += 0.02f * (fabs(leerEMG() - base) - lp); eMax = max(eMax, lp); delay(1); }
  maximo = max(eMax, 5.0f * ruido + 10.0f); umbralOn = 0.25f * maximo; umbralOff = 0.15f * maximo;
  Serial.printf("base %.0f mV, ruido %.1f, max %.0f -> encender %.0f / apagar %.0f\n", base, ruido, maximo, umbralOn, umbralOff);
  parpadear(2, 100);
}

void setup() {
  Serial.begin(115200); pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, HIGH); analogReadResolution(12);
  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2);
  sMano.setPeriodHertz(50); sMano.attach(PIN_MANO, 500, 2500);
  sPulgar.setPeriodHertz(50); sPulgar.attach(PIN_PULGAR, 500, 2500);
  sMuneca.setPeriodHertz(50); sMuneca.attach(PIN_MUNECA, 500, 2500);
  mover(); escribirMuneca();
  delay(800); calibrar(); tUltimaActividad = millis(); tAbierta = millis();
}

void accionPulsoCorto() {                                      // una sola contracción corta (ya se descartó que fuera doble)
  if (modoMuneca) { sentido = -sentido; tUltimoGiro = millis(); Serial.println(sentido > 0 ? F("muneca: pronacion") : F("muneca: supinacion")); }
  else { angMano = MANO_ABIERTA; ajustePulgar = 0; aliviado = true; mover(); tAbierta = millis(); }
}

void loop() {
  static unsigned long tPrev = micros(), tPlot = 0, tBat = 0;
  unsigned long ahora = millis();
  if (micros() - tPrev < 1000) return;                       // 1 kHz
  float dt = (micros() - tPrev) / 1e6f; tPrev = micros();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a') { angMano = MANO_ABIERTA; ajustePulgar = 0; aliviado = true; mover(); tAbierta = ahora; }
    if (c == 'c') { angMano = MANO_CERRADA; aliviado = false; tUltimoCierre = ahora; mover(); }
    if (c == '+' || c == '-') { ajustePulgar += (c == '+') ? 5 : -5; mover(); Serial.printf("pulgar: %.0f\n", pulgarPara(angMano) + ajustePulgar); }
    if (c == '<' || c == '>') { giro += (c == '>') ? 10 : -10; escribirMuneca(); Serial.printf("muneca: %.0f grados\n", giro); }
    if (c == 'n') { giro = 0; escribirMuneca(); }
    if (c == 'm') cambiarModo();
    if (c == 'k') calibrar(); if (c == 'p') plot = !plot;
  }
  env = envolvente(leerEMG());
  float nivel = constrain((env - umbralOff) / max(1.0f, maximo - umbralOff), 0.0f, 1.0f);
  if (!activo && env > umbralOn) { activo = true; tInicio = ahora; }
  if (activo && env < umbralOff) {                             // fin de la contracción
    activo = false; unsigned long dur = ahora - tInicio;
    if (dur >= T_PULSO_MIN && dur <= T_PULSO_MAX) {
      if (pulsoPendiente && ahora - tPulso <= T_DOBLE) { pulsoPendiente = false; cambiarModo(); }   // doble contracción
      else { pulsoPendiente = true; tPulso = ahora; }                                              // espera por si viene otra
    }
  }
  if (pulsoPendiente && !activo && ahora - tPulso > T_DOBLE) { pulsoPendiente = false; accionPulsoCorto(); }
  if (activo) tUltimaActividad = ahora;
  bool sostenida = activo && ahora - tInicio > T_PULSO_MAX;
  if (sostenida) pulsoPendiente = false;
  if (modoMuneca) {
    if (sostenida) { giro += sentido * VEL_MUNECA * nivel * dt; escribirMuneca(); tUltimoGiro = ahora; }
    if (!activo && !pulsoPendiente && ahora - tUltimoGiro > T_REGRESO) cambiarModo();             // regresa sola a la mano
  } else {
    if (sostenida) { angMano += VEL_MANO * nivel * dt; angMano = constrain(angMano, MANO_ABIERTA, MANO_CERRADA); tUltimoCierre = ahora; aliviado = false; ajustePulgar = 0; mover(); }
    if (aliviado && !activo && !pulsoPendiente && angMano <= MANO_ABIERTA + 0.5 && ahora - tAbierta > T_REPOSO) { angMano = MANO_REPOSO; mover(); }   // reposo natural
    if (!aliviado && !activo && ahora - tUltimoCierre > 1500) {  // ya agarró: los dos servos de la mano aflojan un poco (no se calientan)
      angMano = constrain(angMano - ALIVIO, MANO_ABIERTA, MANO_CERRADA); sMano.write((int)round(angMano));
      float p = pulgarPara(angMano) + ajustePulgar; p += (PUL_C > PUL_A ? -ALIVIO_PULGAR : ALIVIO_PULGAR);
      sPulgar.write((int)round(constrain(p, min(PUL_A, PUL_C), max(PUL_A, PUL_C)))); aliviado = true;
    }
  }
  if (!activo && ahora - tUltimaActividad > 2000) { umbralOff += 0.0005f * ((env + 0.15f * maximo) - umbralOff); umbralOn = umbralOff + 0.10f * maximo; }   // umbrales adaptativos
  if (ahora - tBat > 1000) {
    tBat = ahora; float v = bateria();
    if (v > 3.0 && v < BAT_CORTE) { angMano = MANO_ABIERTA; ajustePulgar = 0; aliviado = true; mover(); modoMuneca = false; Serial.println(F("BATERIA BAJA: carga la protesis")); parpadear(10, 60); }
    else if (v > 3.0 && v < BAT_AVISO) parpadear(1, 40);
  }
  if (plot && ahora - tPlot > 20) { tPlot = ahora; Serial.printf("emg:%.0f on:%.0f off:%.0f mano:%.0f muneca:%.0f modo:%d\n", env, umbralOn, umbralOff, angMano, giro, modoMuneca ? 1 : 0); }
}
