/*
  Prótesis izquierda de Adrián · HAND-E · firmware de control (ESP32-S3, p. ej. Seeed XIAO ESP32S3)
  ------------------------------------------------------------------------------------------------
  Un sensor EMG (Sichiray, electrodo seco) en el socket controla la MANO completa con DOS motores:
    * MG995: cierra los 4 dedos por el diferencial (cada dedo se detiene al tocar);
    * MG90S: cierra el PULGAR (varilla de clip a la oreja del pulgar). Va un poco adelantado a los dedos para que
      la punta del índice llegue a la punta del pulgar (pinza) y se detiene solo si toca el objeto.
  Uso: mantener la contracción = cerrar (más fuerte = más rápido); relajar = la mano se queda donde está (sostiene);
       una contracción CORTA = abrir.
  La muñeca es pasiva (se gira con la otra mano, clics cada 20°): no la controla el programa.
  Calibración personalizada con umbrales adaptativos: al encender, 3 s en reposo y 3 s apretando fuerte; el umbral
  se ajusta solo mientras el brazo está en reposo.
  Seguridad: límites de los servos, alivio después de agarrar (no se calientan), batería baja = abre y se detiene.

  Conexiones (XIAO ESP32S3):
    A0 (D0)  <- señal del sensor EMG (SIG)            sensor VCC <- 3.3 V, GND común
    A1 (D1)  <- divisor de batería 100 kΩ / 33 kΩ (2S: 8.4 V -> 2.08 V)
    D2       -> señal del servo de los DEDOS (MG995)   servos: alimentar con el convertidor 6 V / 3 A, NO del ESP32
    D3       -> señal del servo del PULGAR (MG90S)
  Librería: ESP32Servo (Kevin Harrington), desde el Administrador de librerías de Arduino.
  Comandos por USB (115200) para la demostración: a = abrir, c = cerrar, k = recalibrar, p = mostrar la señal,
                                                   + / - = mover solo el pulgar 5° (para ajustar PUL_A y PUL_C).
  AJUSTE DEL PULGAR (una vez): con 'a' el pulgar debe quedar abierto. Si al cerrar el pulgar se ABRE, intercambia
  los valores de PUL_A y PUL_C. El recorrido calculado del cuerno es de ~130°.
*/
#include <ESP32Servo.h>

const int PIN_EMG = A0, PIN_BAT = A1, PIN_MANO = D2, PIN_PULGAR = D3, PIN_LED = LED_BUILTIN;
const float MANO_ABIERTA = 0.0, MANO_CERRADA = 80.0;          // grados del servo de los dedos (el diferencial aprieta a ~78°)
const float PUL_A = 25.0, PUL_C = 155.0;                       // grados del servo del pulgar: abierto / cerrado (pinza)
const float ADELANTO = 0.8;                                    // el pulgar llega a cerrado cuando los dedos van al 80 %
const float VEL_MANO = 120.0;                                  // grados por segundo con contracción máxima
const float ALIVIO = 3.0, ALIVIO_PULGAR = 4.0;                 // grados que aflojan 1.5 s después de agarrar
const float BAT_AVISO = 6.6, BAT_CORTE = 6.4, DIVISOR = (100.0 + 33.0) / 33.0;
const unsigned long T_PULSO_MIN = 60, T_PULSO_MAX = 380;

Servo sMano, sPulgar;
float base = 0, ruido = 1, maximo = 100, env = 0, umbralOn = 30, umbralOff = 18, angMano = MANO_ABIERTA, ajustePulgar = 0;
bool activo = false, plot = false, aliviado = true;
unsigned long tInicio = 0, tUltimaActividad = 0, tUltimoCierre = 0;

float leerEMG() { return analogReadMilliVolts(PIN_EMG); }
float bateria() { return analogReadMilliVolts(PIN_BAT) / 1000.0 * DIVISOR; }
void parpadear(int n, int ms) { for (int i = 0; i < n; i++) { digitalWrite(PIN_LED, LOW); delay(ms); digitalWrite(PIN_LED, HIGH); delay(ms); } }

float pulgarPara(float angDedos) {                             // el pulgar sigue a los dedos, un poco adelantado
  float f = constrain((angDedos - MANO_ABIERTA) / ((MANO_CERRADA - MANO_ABIERTA) * ADELANTO), 0.0f, 1.0f);
  return PUL_A + f * (PUL_C - PUL_A);
}

void mover() {
  sMano.write((int)round(angMano));
  float p = pulgarPara(angMano) + ajustePulgar;
  if (!aliviado || angMano <= MANO_ABIERTA + 0.5f) sPulgar.write((int)round(constrain(p, min(PUL_A, PUL_C), max(PUL_A, PUL_C))));
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
  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1);
  sMano.setPeriodHertz(50); sMano.attach(PIN_MANO, 500, 2500);
  sPulgar.setPeriodHertz(50); sPulgar.attach(PIN_PULGAR, 500, 2500);
  aliviado = false; mover(); aliviado = true;
  delay(800); calibrar(); tUltimaActividad = millis();
}

void loop() {
  static unsigned long tPrev = micros(), tPlot = 0, tBat = 0;
  unsigned long ahora = millis();
  if (micros() - tPrev < 1000) return;                       // 1 kHz
  float dt = (micros() - tPrev) / 1e6f; tPrev = micros();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a') { angMano = MANO_ABIERTA; ajustePulgar = 0; aliviado = true; }
    if (c == 'c') { angMano = MANO_CERRADA; aliviado = false; tUltimoCierre = ahora; }
    if (c == '+') { ajustePulgar += 5; aliviado = false; Serial.printf("pulgar: %.0f\n", pulgarPara(angMano) + ajustePulgar); }
    if (c == '-') { ajustePulgar -= 5; aliviado = false; Serial.printf("pulgar: %.0f\n", pulgarPara(angMano) + ajustePulgar); }
    if (c == 'k') calibrar(); if (c == 'p') plot = !plot;
  }
  env = envolvente(leerEMG());
  float nivel = constrain((env - umbralOff) / max(1.0f, maximo - umbralOff), 0.0f, 1.0f);
  if (!activo && env > umbralOn) { activo = true; tInicio = ahora; }
  if (activo && env < umbralOff) {                             // fin de la contracción: si fue corta, abre
    activo = false; unsigned long dur = ahora - tInicio;
    if (dur >= T_PULSO_MIN && dur <= T_PULSO_MAX) { angMano = MANO_ABIERTA; ajustePulgar = 0; aliviado = true; }
  }
  if (activo) tUltimaActividad = ahora;
  if (activo && ahora - tInicio > T_PULSO_MAX) { angMano += VEL_MANO * nivel * dt; tUltimoCierre = ahora; aliviado = false; ajustePulgar = 0; }
  if (!aliviado && !activo && ahora - tUltimoCierre > 1500) {  // ya agarró: los dos servos aflojan un poco (no se calientan)
    angMano -= ALIVIO; angMano = constrain(angMano, MANO_ABIERTA, MANO_CERRADA);
    float p = pulgarPara(angMano) + ajustePulgar; p += (PUL_C > PUL_A ? -ALIVIO_PULGAR : ALIVIO_PULGAR);
    sMano.write((int)round(angMano)); sPulgar.write((int)round(constrain(p, min(PUL_A, PUL_C), max(PUL_A, PUL_C)))); aliviado = true;
  }
  angMano = constrain(angMano, MANO_ABIERTA, MANO_CERRADA);
  if (!aliviado || angMano <= MANO_ABIERTA + 0.5f) mover();
  if (!activo && ahora - tUltimaActividad > 2000) { umbralOff += 0.0005f * ((env + 0.15f * maximo) - umbralOff); umbralOn = umbralOff + 0.10f * maximo; }   // umbrales adaptativos
  if (ahora - tBat > 1000) {
    tBat = ahora; float v = bateria();
    if (v > 3.0 && v < BAT_CORTE) { angMano = MANO_ABIERTA; aliviado = true; sMano.write(0); sPulgar.write((int)PUL_A); Serial.println(F("BATERIA BAJA: carga la protesis")); parpadear(10, 60); }
    else if (v > 3.0 && v < BAT_AVISO) parpadear(1, 40);
  }
  if (plot && ahora - tPlot > 20) { tPlot = ahora; Serial.printf("emg:%.0f on:%.0f off:%.0f mano:%.0f pulgar:%.0f\n", env, umbralOn, umbralOff, angMano, pulgarPara(angMano) + ajustePulgar); }
}
