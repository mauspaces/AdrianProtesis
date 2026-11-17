/*
  Prótesis izquierda de Adrián · HAND-E · firmware de control (ESP32-S3, p. ej. Seeed XIAO ESP32S3)
  ------------------------------------------------------------------------------------------------
  Un sensor EMG (Sichiray, electrodo seco) en el socket controla:
    * MANO (modo normal): mantener la contracción = cerrar (más fuerte = más rápido); relajar = la mano se queda
      donde está (sostiene); una contracción CORTA = abrir.
    * DOBLE contracción corta = cambiar a modo MUÑECA (la mano hace un pequeño "saludo" para avisar).
      En modo muñeca: mantener = girar; contracción corta = cambiar el sentido; doble = volver a MANO.
      Si no hace nada por 6 s, regresa solo a MANO.
  Calibración personalizada con umbrales adaptativos: al encender, 3 s en reposo y 3 s apretando fuerte; los umbrales
  se ajustan solos mientras está en reposo.
  Seguridad: límites de los servos, alivio del servo después de agarrar, batería baja = abre y se detiene.

  Conexiones (XIAO ESP32S3):
    A0 (D0)  <- señal del módulo EMG (SIG)            EMG VCC <- 3.3 V, GND común
    A1 (D1)  <- divisor de batería 100 kΩ / 33 kΩ (2S: 8.4 V -> 2.08 V)
    D2       -> señal del servo de la MANO (MG995)     servos: alimentar con el convertidor 6 V / 3 A, NO del ESP32
    D3       -> señal del servo de la MUÑECA (MG90S)
  Librería: ESP32Servo (Kevin Harrington), desde el Administrador de librerías de Arduino.
  Comandos por USB (115200, para la demostración): a = abrir, c = cerrar, n = muñeca neutra, i/d = muñeca izquierda/derecha,
  k = recalibrar, p = mostrar la señal (para el Serial Plotter).
*/
#include <ESP32Servo.h>

// ---------------- ajustes (de los cálculos del diseño: datos/49_garra_adaptativa.json y 47_muneca_y_uniones.json)
const int PIN_EMG = A0, PIN_BAT = A1, PIN_MANO = D2, PIN_MUNECA = D3, PIN_LED = LED_BUILTIN;
const float MANO_ABIERTA = 0.0, MANO_CERRADA = 90.0;          // grados del servo de la mano (el diferencial cierra en ~90°)
const float MUNECA_NEUTRA = 90.0, MUNECA_GIRO = 77.0;          // neutra = pulgar arriba; ±77° (el tope físico está en ±80°)
const float VEL_MANO = 120.0, VEL_MUNECA = 60.0;               // grados por segundo con contracción máxima
const float ALIVIO = 3.0;                                      // grados que afloja el servo 1.5 s después de agarrar
const float BAT_AVISO = 6.6, BAT_CORTE = 6.4, DIVISOR = (100.0 + 33.0) / 33.0;
const unsigned long T_PULSO_MIN = 60, T_PULSO_MAX = 380, T_DOBLE = 650, T_MUNECA_SOLA = 6000;

Servo sMano, sMuneca;
float base = 0, ruido = 1, maximo = 100, env = 0, umbralOn = 30, umbralOff = 18;
float angMano = MANO_ABIERTA, angMuneca = MUNECA_NEUTRA, dirMuneca = 1;
bool modoMuneca = false, activo = false, plot = false, aliviado = true;
unsigned long tInicio = 0, tUltimoPulso = 0, tUltimaActividad = 0, tUltimoCierre = 0;
int pulsosPendientes = 0;

float leerEMG() { return analogReadMilliVolts(PIN_EMG); }
float bateria() { return analogReadMilliVolts(PIN_BAT) / 1000.0 * DIVISOR; }

void mover(Servo &s, float a) { s.write((int)round(a)); }

void parpadear(int n, int ms) {
  for (int i = 0; i < n; i++) { digitalWrite(PIN_LED, LOW); delay(ms); digitalWrite(PIN_LED, HIGH); delay(ms); }
}

void saludo() {                       // aviso de cambio de modo: los dedos se mueven poquito dos veces
  float a = angMano;
  for (int i = 0; i < 2; i++) { mover(sMano, a + 8); delay(140); mover(sMano, a); delay(140); }
}

// envolvente: |x - base| filtrada (sirve si el módulo da la señal cruda o ya rectificada)
float envolvente(float x) {
  static float lp = 0;
  float r = fabs(x - base);
  lp += 0.02f * (r - lp);             // ~6 Hz a 1 kHz de muestreo
  return lp;
}

void calibrar() {
  Serial.println(F("CALIBRACION: relaja el brazo 3 s..."));
  parpadear(3, 150);
  double s = 0, s2 = 0; int n = 0; unsigned long t = millis();
  while (millis() - t < 3000) { float x = leerEMG(); s += x; s2 += x * x; n++; delay(1); }
  base = s / n; ruido = sqrt(max(1e-3, s2 / n - base * base));
  float eMax = 0; t = millis();
  Serial.println(F("Ahora APRIETA fuerte 3 s..."));
  parpadear(1, 400);
  float lp = 0;
  while (millis() - t < 3000) { float r = fabs(leerEMG() - base); lp += 0.02f * (r - lp); eMax = max(eMax, lp); delay(1); }
  maximo = max(eMax, 5.0f * ruido + 10.0f);
  umbralOn = 0.25f * maximo; umbralOff = 0.15f * maximo;
  Serial.printf("base %.0f mV, ruido %.1f, max %.0f -> encender %.0f / apagar %.0f\n", base, ruido, maximo, umbralOn, umbralOff);
  parpadear(2, 100);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, HIGH);
  analogReadResolution(12);
  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1);
  sMano.setPeriodHertz(50); sMuneca.setPeriodHertz(50);
  sMano.attach(PIN_MANO, 500, 2500); sMuneca.attach(PIN_MUNECA, 500, 2500);
  mover(sMano, angMano); mover(sMuneca, angMuneca);
  delay(800);
  calibrar();
  tUltimaActividad = millis();
}

void comandos() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'a') angMano = MANO_ABIERTA;
    if (c == 'c') angMano = MANO_CERRADA;
    if (c == 'n') angMuneca = MUNECA_NEUTRA;
    if (c == 'i') angMuneca = MUNECA_NEUTRA - MUNECA_GIRO;
    if (c == 'd') angMuneca = MUNECA_NEUTRA + MUNECA_GIRO;
    if (c == 'k') calibrar();
    if (c == 'p') plot = !plot;
  }
}

void eventoPulso(unsigned long ahora) {                      // contracción corta
  if (pulsosPendientes == 1 && ahora - tUltimoPulso < T_DOBLE) { pulsosPendientes = 2; }
  else { pulsosPendientes = 1; }
  tUltimoPulso = ahora;
}

void loop() {
  static unsigned long tPrev = micros(); static unsigned long tPlot = 0, tBat = 0;
  unsigned long ahora = millis();
  if (micros() - tPrev < 1000) return;                       // 1 kHz
  float dt = (micros() - tPrev) / 1e6f; tPrev = micros();
  comandos();
  env = envolvente(leerEMG());
  float nivel = constrain((env - umbralOff) / max(1.0f, maximo - umbralOff), 0.0f, 1.0f);

  // ---- detectar contracciones (con histéresis)
  if (!activo && env > umbralOn) { activo = true; tInicio = ahora; }
  if (activo && env < umbralOff) {
    activo = false; unsigned long dur = ahora - tInicio;
    if (dur >= T_PULSO_MIN && dur <= T_PULSO_MAX) eventoPulso(ahora);
  }
  bool sostenido = activo && (ahora - tInicio > T_PULSO_MAX);
  if (activo) tUltimaActividad = ahora;

  // ---- resolver pulsos (se espera T_DOBLE para saber si es doble)
  if (pulsosPendientes == 2) {
    modoMuneca = !modoMuneca; pulsosPendientes = 0; saludo(); tUltimaActividad = ahora;
    Serial.println(modoMuneca ? F("modo MUNECA") : F("modo MANO"));
  } else if (pulsosPendientes == 1 && ahora - tUltimoPulso > T_DOBLE) {
    pulsosPendientes = 0;
    if (modoMuneca) dirMuneca = -dirMuneca; else { angMano = MANO_ABIERTA; aliviado = true; }
  }

  // ---- mover según el modo
  if (sostenido) {
    if (modoMuneca) angMuneca += dirMuneca * VEL_MUNECA * nivel * dt;
    else { angMano += VEL_MANO * nivel * dt; tUltimoCierre = ahora; aliviado = false; }
  }
  if (!modoMuneca && !aliviado && !activo && ahora - tUltimoCierre > 1500) { angMano -= ALIVIO; aliviado = true; }   // afloja un poco: el servo no se calienta
  if (modoMuneca && ahora - tUltimaActividad > T_MUNECA_SOLA) { modoMuneca = false; saludo(); Serial.println(F("modo MANO (automatico)")); }
  angMano = constrain(angMano, MANO_ABIERTA, MANO_CERRADA);
  angMuneca = constrain(angMuneca, MUNECA_NEUTRA - MUNECA_GIRO, MUNECA_NEUTRA + MUNECA_GIRO);
  mover(sMano, angMano); mover(sMuneca, angMuneca);

  // ---- umbrales adaptativos: en reposo largo, el piso de la señal se actualiza despacio
  if (!activo && ahora - tUltimaActividad > 2000) {
    float piso = env; umbralOff += 0.0005f * ((piso + 0.15f * maximo) - umbralOff); umbralOn = umbralOff + 0.10f * maximo;
  }

  // ---- batería y LED
  if (ahora - tBat > 1000) {
    tBat = ahora; float v = bateria();
    if (v > 3.0 && v < BAT_CORTE) { angMano = MANO_ABIERTA; mover(sMano, angMano); Serial.println(F("BATERIA BAJA: carga la protesis")); parpadear(10, 60); }
    else if (v > 3.0 && v < BAT_AVISO) parpadear(1, 40);
  }
  digitalWrite(PIN_LED, modoMuneca ? ((ahora / 250) % 2) : HIGH);
  if (plot && ahora - tPlot > 20) { tPlot = ahora; Serial.printf("emg:%.0f on:%.0f off:%.0f mano:%.0f muneca:%.0f\n", env, umbralOn, umbralOff, angMano, angMuneca); }
}
