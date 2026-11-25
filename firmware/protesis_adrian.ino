/*
  Prótesis izquierda de Adrián · Hand-e · firmware v8 «Studio» (ESP32-S3, Seeed XIAO ESP32S3)
  ------------------------------------------------------------------------------------------
  Un sensor EMG (Sichiray, electrodo seco) en el socket controla TRES motores:
    * MG995: cierra los 4 dedos por el diferencial (cada dedo se detiene al tocar);
    * MG90S del pulgar: cierra junto con los dedos, un poco adelantado (pinza punta con punta);
    * MG90S de la muñeca: gira la mano ±80° desde la neutra (pulgar arriba).

  CÓMO SE USA (sin esperas: la mano empieza a moverse ~0.1 s después de que Adrián contrae)
    Mano abierta ...... APRIETA y sostén = cierra (más fuerte, más rápido). SUELTA = se queda donde está.
                        PULSO CORTO = entra al modo muñeca (la muñeca hace 2 «tics» para avisar).
    Mano a medio cerrar PULSO CORTO = abre. APRIETA otra vez = sigue cerrando.
    Agarrando algo .... PULSO CORTO = abre. APRIETA y sostén = gira la muñeca SIN soltar (para servir agua);
                        cada nueva contracción gira hacia el lado contrario.
    Modo muñeca ....... APRIETA y sostén = gira (cada vez al lado contrario). PULSO CORTO = vuelve a la mano (1 tic).
                        Regresa sola a la mano tras 5 s sin moverse.
    Abierta y sin usarse 3 s: postura de reposo natural (dedos un poco doblados).
  Un pulso corto (< 350 ms) también puede ser un movimiento del brazo sin querer. Lo decide un MODELO de aprendizaje
  automático (análisis discriminante lineal) entrenado con las señales de Adrián en Hand-e Studio o con
  herramientas/entrenar_modelo.py. El modelo se guarda en la memoria de la ESP32: no hay que volver a compilar.
  Sin modelo usa una regla fija. Lo poco que se movió durante un pulso se deshace al soltar.

  ENCENDIDO = calibración de 6 s: RELAJA 3 s · la muñeca hace 1 tic = APRIETA fuerte 3 s · 2 tics = lista.
    Si sale mal (no apretó o el electrodo no toca: 3 tics) usa la última calibración buena guardada.
  SEGURIDAD: el Bluetooth y la app NO pueden mover la mano: solo el músculo de Adrián la mueve. Batería baja: la
    muñeca avisa con 3 tics y la mano ya no cierra ni gira, solo abre (suelta cuando él quiera). Alivio después de
    agarrar (los servos no se calientan). El modo de entrenamiento se apaga solo a los 15 min.

  Conexiones (XIAO ESP32S3):
    A0 (D0) <- señal del sensor EMG (SIG)           sensor VCC <- 3.3 V, GND común
    A1 (D1) <- divisor de batería 100 kΩ / 33 kΩ (2S: 8.4 V -> 2.08 V)
    D2 -> dedos (MG995) · D3 -> pulgar (MG90S) · D4 -> muñeca (MG90S)
    Servos y XIAO: convertidor de 5 V / 5 A + capacitor de 1000 µF en la línea de los servos (NO de la ESP32).
  Librería: ESP32Servo (Kevin Harrington). Bluetooth y memoria vienen con el paquete esp32 (3.x).
  Bluetooth: se anuncia como «Hand-e» (servicio UART de Nordic). App: https://mauspaces.github.io/AdrianProtesis/studio.html
  Comandos (USB a 115200 o Bluetooth; una letra, o una línea terminada en Enter):
    v datos en vivo · i información · u uso acumulado · k calibrar · r modo entrenamiento (la mano no se mueve y
    reporta cada contracción) · P,sens,0.25 / P,vel,1.0 ajustes · M,<20 valores> guarda el modelo · M0 lo borra ·
    BORRAR_USO.  Solo por USB (taller): a abrir · c cerrar · +/- pulgar 5° · </> muñeca 10° · n neutra ·
    m modo muñeca · p gráfica para el Serial Plotter · h ayuda.
  Lo que manda (una línea por dato): T,señal,umbral on,umbral off,dedos°,muñeca°,banderas (25 por segundo con v) ·
    S,batería V,máximo,fuente del modelo,sens,vel,MTU · EV,ms,5 rasgos · DEC,ms,a propósito 0/1,puntaje,acción ·
    LAT,ms · CALI,1/2 · CAL,base,máximo,umbral,fuente,buena · INFO,... · USO,... · BAT,estado,V · OK,... · ERR,...
  AJUSTES (una vez): si al cerrar el pulgar se ABRE, intercambia PUL_A y PUL_C. Si la muñeca gira al revés,
    MUNECA_SENTIDO = -1. La neutra (pulgar arriba) es MUNECA_NEUTRA: ponla al montar el cuerno.
*/
#include <ESP32Servo.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <stdarg.h>
#if __has_include("modelo_emg.h")
#include "modelo_emg.h"                                        // modelo de respaldo escrito por entrenar_modelo.py
#define HAY_MODELO_H 1
static_assert(MODELO_N == 5, "modelo_emg.h debe tener 5 rasgos");
#else
#define HAY_MODELO_H 0
#endif

#define VERSION_FW "v8"
const int PIN_EMG = A0, PIN_BAT = A1, PIN_MANO = D2, PIN_PULGAR = D3, PIN_MUNECA = D4, PIN_LED = LED_BUILTIN;
const float MANO_ABIERTA = 0.0, MANO_REPOSO = 16.0, MANO_CERRADA = 64.0;   // grados del servo de los dedos. Mano v6: a 63° los 4 dedos ya cerraron completos (carro 8.7 mm); más allá el MG995 empuja contra el fondo y puede romper el PLA
const float PUL_A = 25.0, PUL_C = 126.0;                      // grados del servo del pulgar: abierto / cerrado (pinza). Mano v6: la oreja del pulgar subió 8 mm y el servo gira 101° (antes 155°)
const float ADELANTO = 0.8;                                    // el pulgar llega a cerrado cuando los dedos van al 80 %
const float MUNECA_NEUTRA = 90.0, MUNECA_LIMITE = 77.0;        // grados del servo de la muñeca (tope físico a 80°)
const int MUNECA_SENTIDO = 1;                                  // 1 o -1 (para que "positivo" sea pronación)
const float VEL_MANO = 140.0, VEL_MUNECA = 90.0;               // grados por segundo con contracción máxima (vel = 1)
const float ARRANQUE = 0.35;                                   // velocidad relativa mientras todavía puede ser un pulso
const unsigned long T_ARRANQUE = 90, T_CORTO = 350;            // ms: empieza a moverse / hasta aquí es un pulso corto
const unsigned long T_REPOSO = 3000, T_MODO = 5000, T_ALIVIO = 1500, T_REGISTRO = 15UL * 60 * 1000;
const float ALIVIO = 3.0, ALIVIO_PULGAR = 4.0;                 // grados que aflojan después de agarrar
const float BAT_AVISO = 6.8, BAT_CORTE = 6.5, DIVISOR = (100.0 + 33.0) / 33.0;   // 2S: 3.4 y 3.25 V por celda

Servo sMano, sPulgar, sMuneca;
Preferences mem;
float base = 0, ruido = 1, maximo = 100, env = 0, umbralOn = 30, umbralOff = 18;
float sens = 0.25f, vel = 1.0f;                                // ajustes de Studio: umbral (fracción de su fuerza) y velocidad
float angMano = MANO_ABIERTA, ajustePulgar = 0, giro = 0;      // giro: grados desde la neutra (+ = pronación)
int sentidoGiro = 1;
bool modoMuneca = false, agarrado = false, aliviado = true, plot = false, registro = false;
// la contracción en curso
bool activo = false, seMovio = false;
unsigned long tInicio = 0, tUltimaActividad = 0, tUltimoMov = 0, tAbierta = 0, evTPico = 0, evN = 0, tRegistro = 0;
float evPico = 0, evArea = 0, evSuma = 0;
float manoInicio = 0, giroInicio = 0;
bool agarradoInicio = false, munecaInicio = false;
// modelo, uso y batería
struct Modelo { float media[5], escala[5], w[5], b; uint16_t n1, n0; uint8_t accM, accR; };
Modelo mod; int fuenteModelo = 0;                              // 0 regla fija · 1 modelo_emg.h · 2 entrenado (memoria)
struct Uso { uint32_t cierres, aperturas, giros, ignorados, minutos, encendidos; };
Uso uso = {0, 0, 0, 0, 0, 0}; bool usoCambio = false;
float vBat = 0; int estadoBat = 0;                             // 0 bien · 1 aviso · 2 corte

// ------------------------------------------------------------------ salida por USB y por Bluetooth
enum { A_USB = 1, A_BLE = 2, A_TODOS = 3 };
#define UUID_SERV "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
BLEServer *servidor = nullptr;
BLECharacteristic *txC = nullptr;
volatile bool bleConectado = false, reanunciar = false;
uint16_t bleMTU = 23;
QueueHandle_t colaBLE;
bool streamUSB = false, streamBLE = false;
int registroOrigen = 0;

void salida(const char *s, int a) {
  if (a & A_USB) Serial.print(s);
  if ((a & A_BLE) && bleConectado && txC) {                    // en trozos del tamaño que acepta el teléfono
    size_t n = strlen(s), paso = (bleMTU > 23) ? (size_t)(bleMTU - 3) : 20;
    for (size_t i = 0; i < n; i += paso) { txC->setValue((uint8_t *)s + i, min(paso, n - i)); txC->notify(); }
  }
}
void enviar(int a, const char *fmt, ...) {
  char b[240]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); salida(b, a);
}

class CbServidor : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { bleConectado = true; }
  void onDisconnect(BLEServer *s) override { bleConectado = false; reanunciar = true; }
};
class CbRx : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {               // corre en el núcleo del Bluetooth: solo encola
    String v = c->getValue();
    for (unsigned i = 0; i < v.length(); i++) { char ch = v[i]; xQueueSend(colaBLE, &ch, 0); }
  }
};
void iniciarBLE() {
  colaBLE = xQueueCreate(512, 1);
  BLEDevice::init("Hand-e"); BLEDevice::setMTU(185);           // sin datos personales en el nombre
  servidor = BLEDevice::createServer(); servidor->setCallbacks(new CbServidor());
  BLEService *srv = servidor->createService(UUID_SERV);
  txC = srv->createCharacteristic(UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  txC->addDescriptor(new BLE2902());
#endif
  BLECharacteristic *rx = srv->createCharacteristic(UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new CbRx());
  srv->start();
  BLEAdvertising *ad = BLEDevice::getAdvertising(); ad->addServiceUUID(UUID_SERV); ad->setScanResponse(true);
  BLEDevice::startAdvertising();
}

// ------------------------------------------------------------------ motores
float leerEMG() { return analogReadMilliVolts(PIN_EMG); }
float bateria() { return analogReadMilliVolts(PIN_BAT) / 1000.0 * DIVISOR; }

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
void tic(int n) {                                              // aviso que Adrián siente y el jurado ve: la muñeca se sacude n veces
  for (int i = 0; i < n; i++) {
    float g = constrain(giro + (giro > 0 ? -7.0f : 7.0f), -MUNECA_LIMITE, MUNECA_LIMITE);
    sMuneca.write((int)round(MUNECA_NEUTRA + MUNECA_SENTIDO * g)); digitalWrite(PIN_LED, LOW); delay(110);
    escribirMuneca(); digitalWrite(PIN_LED, HIGH); delay(130);
  }
}
void abrir() { angMano = MANO_ABIERTA; ajustePulgar = 0; agarrado = false; aliviado = true; mover(); tAbierta = millis(); }
void ponerModoMuneca(bool si) { modoMuneca = si; sentidoGiro = 1; tUltimoMov = millis(); tic(si ? 2 : 1); }

float envolvente(float x) {                                    // |x - base| filtrada (sirve con señal cruda o rectificada)
  static float lp = 0; lp += 0.02f * (fabs(x - base) - lp); return lp;
}

// ------------------------------------------------------------------ el modelo
// Rasgos de una contracción: duración (s), pico y área relativos a su fuerza máxima de la calibración,
// tiempo hasta el pico (s) y forma (promedio / pico). Studio y entrenar_modelo.py usan exactamente estos cinco.
void rasgos(unsigned long durMs, float *x) {
  x[0] = durMs / 1000.0f;
  x[1] = evPico / maximo;
  x[2] = evArea / maximo;
  x[3] = evTPico / 1000.0f;
  x[4] = evN ? (evSuma / evN) / max(evPico, 0.001f) : 0.0f;
}
void cargarModelo() {
  if (mem.getBytesLength("modelo") == sizeof(Modelo) && mem.getBytes("modelo", &mod, sizeof(Modelo)) == sizeof(Modelo)) { fuenteModelo = 2; return; }
#if HAY_MODELO_H
  for (int i = 0; i < 5; i++) { mod.media[i] = MODELO_MEDIA[i]; mod.escala[i] = MODELO_ESCALA[i]; mod.w[i] = MODELO_W[i]; }
  mod.b = MODELO_B; mod.n1 = mod.n0 = 0; mod.accM = mod.accR = 0; fuenteModelo = 1;
#else
  fuenteModelo = 0;
#endif
}
float puntaje(const float *x) {                                // > 0: el pulso fue a propósito
  if (fuenteModelo == 0) return 10.0f * min(x[0] - 0.08f, x[1] - 0.35f);   // regla fija: 80 ms y 35 % de su fuerza
  float s = mod.b;
  for (int i = 0; i < 5; i++) s += mod.w[i] * (x[i] - mod.media[i]) / mod.escala[i];
  return s;
}
bool guardarModelo(char *linea) {                              // M,media×5,escala×5,w×5,b,n1,n0,accM,accR
  float v[20]; int k = 0; char *ctx;
  for (char *t = strtok_r(linea + 2, ",", &ctx); t && k < 20; t = strtok_r(nullptr, ",", &ctx)) v[k++] = atof(t);
  if (k != 20) return false;
  Modelo m;
  for (int i = 0; i < 5; i++) {
    m.media[i] = v[i]; m.escala[i] = v[5 + i]; m.w[i] = v[10 + i];
    if (!isfinite(v[i]) || !(v[5 + i] > 0) || !isfinite(v[10 + i]) || fabs(v[10 + i]) > 1000) return false;
  }
  if (!isfinite(v[15])) return false;
  m.b = v[15]; m.n1 = (uint16_t)v[16]; m.n0 = (uint16_t)v[17]; m.accM = (uint8_t)constrain(v[18], 0, 100); m.accR = (uint8_t)constrain(v[19], 0, 100);
  mod = m; mem.putBytes("modelo", &mod, sizeof(Modelo)); fuenteModelo = 2;
  return true;
}

// ------------------------------------------------------------------ calibración y memoria
void aplicarUmbrales() { umbralOn = sens * maximo; umbralOff = max(0.05f, sens - 0.10f) * maximo; }
void calibrar() {
  enviar(A_TODOS, "CALI,1\n");                                 // relaja
  double s = 0, s2 = 0; int n = 0; unsigned long t = millis();
  while (millis() - t < 3000) { float x = leerEMG(); s += x; s2 += x * x; n++; delay(1); }
  float b = s / n, r = sqrt(max(1e-3, s2 / n - (double)b * b));
  enviar(A_TODOS, "CALI,2\n"); tic(1);                         // aprieta
  float lp = 0, eMax = 0; t = millis();
  while (millis() - t < 3000) { lp += 0.02f * (fabs(leerEMG() - b) - lp); eMax = max(eMax, lp); delay(1); }
  base = b; ruido = r;
  bool buena = eMax > 6.0f * r + 20.0f;
  if (buena) { maximo = eMax; mem.putFloat("max", maximo); }
  else { float m = mem.getFloat("max", 0); maximo = (m > 0) ? m : max(eMax, 5.0f * r + 10.0f); }
  aplicarUmbrales();
  enviar(A_TODOS, "CAL,%.1f,%.1f,%.1f,%d,%d\n", base, maximo, umbralOn, fuenteModelo, buena ? 1 : 0);
  tic(buena ? 2 : 3); tUltimaActividad = millis();
}
void guardarUso() { mem.putBytes("uso", &uso, sizeof(Uso)); usoCambio = false; }
void info(int a) {
  enviar(a, "INFO,%s,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f\n", VERSION_FW, fuenteModelo, mod.accM, mod.accR, mod.n1, mod.n0,
         sens, vel, vBat, base, maximo, umbralOn, umbralOff);
}
void usoTexto(int a) { enviar(a, "USO,%lu,%lu,%lu,%lu,%lu,%lu\n", (unsigned long)uso.cierres, (unsigned long)uso.aperturas, (unsigned long)uso.giros,
                              (unsigned long)uso.ignorados, (unsigned long)uso.minutos, (unsigned long)uso.encendidos); }
void ayuda() {
  Serial.println(F("v en vivo | i info | u uso | k calibrar | r entrenamiento | P,sens,0.25 | P,vel,1.0 | M,... modelo | M0 | BORRAR_USO"));
  Serial.println(F("Taller (solo USB): a abrir | c cerrar | +/- pulgar | </> muneca | n neutra | m modo muneca | p grafica"));
  Serial.println(fuenteModelo == 2 ? F("Pulsos cortos: modelo entrenado con Adrian (memoria)") : fuenteModelo == 1 ? F("Pulsos cortos: modelo de modelo_emg.h")
                                   : F("Pulsos cortos: regla fija (aun no hay modelo)"));
}

// ------------------------------------------------------------------ comandos (USB y Bluetooth)
void ejecutarLetra(char c, int origen, unsigned long ahora) {
  if (strchr("ac+-<>nmp", c) && origen == A_BLE) { enviar(A_BLE, "ERR,la app no mueve la protesis\n"); return; }
  switch (c) {
    case 'a': abrir(); break;
    case 'c': angMano = MANO_CERRADA; agarrado = true; aliviado = false; tUltimoMov = ahora; mover(); break;
    case '+': case '-': ajustePulgar += (c == '+') ? 5 : -5; mover(); enviar(A_USB, "pulgar: %.0f\n", pulgarPara(angMano) + ajustePulgar); break;
    case '<': case '>': giro += (c == '>') ? 10 : -10; escribirMuneca(); enviar(A_USB, "muneca: %.0f grados\n", giro); break;
    case 'n': giro = 0; escribirMuneca(); break;
    case 'm': ponerModoMuneca(!modoMuneca); break;
    case 'p': plot = !plot; break;
    case 'k':
      if (agarrado || activo) { enviar(origen, "ERR,suelta el objeto antes de calibrar\n"); break; }
      calibrar(); break;
    case 'r':
      registro = !registro; registroOrigen = origen; tRegistro = ahora;
      if (registro && modoMuneca) modoMuneca = false;
      enviar(A_TODOS, "REG,%d\n", registro ? 1 : 0);
      if (registro) enviar(origen, "CAL,%.1f,%.1f,%.1f,%d,1\n", base, maximo, umbralOn, fuenteModelo);
      break;
    case 'v': if (origen == A_BLE) streamBLE = !streamBLE; else streamUSB = !streamUSB; break;
    case 'i': info(origen); break;
    case 'u': usoTexto(origen); break;
    case 'h': if (origen == A_USB) ayuda(); break;
    default: break;
  }
}
void ejecutarLinea(char *l, int origen, unsigned long ahora) {
  if (l[1] == 0) { ejecutarLetra(l[0], origen, ahora); return; }
  if (!strncmp(l, "M,", 2)) { if (guardarModelo(l)) { enviar(A_TODOS, "OK,modelo\n"); info(origen); } else enviar(origen, "ERR,modelo invalido\n"); }
  else if (!strcmp(l, "M0")) { mem.remove("modelo"); cargarModelo(); enviar(A_TODOS, "OK,modelo borrado\n"); info(origen); }
  else if (!strncmp(l, "P,", 2)) {
    char *ctx; strtok_r(l, ",", &ctx); char *nom = strtok_r(nullptr, ",", &ctx); char *val = strtok_r(nullptr, ",", &ctx);
    if (!nom || !val) { enviar(origen, "ERR,ajuste\n"); return; }
    float x = atof(val);
    if (!strcmp(nom, "sens")) { sens = constrain(x, 0.12f, 0.45f); mem.putFloat("sens", sens); aplicarUmbrales(); enviar(A_TODOS, "OK,sens,%.2f\n", sens); }
    else if (!strcmp(nom, "vel")) { vel = constrain(x, 0.5f, 1.5f); mem.putFloat("vel", vel); enviar(A_TODOS, "OK,vel,%.2f\n", vel); }
    else enviar(origen, "ERR,ajuste\n");
  }
  else if (!strcmp(l, "BORRAR_USO")) { uso = {0, 0, 0, 0, 0, 0}; guardarUso(); usoTexto(A_TODOS); }
  else enviar(origen, "ERR,comando\n");
}
char lineaUSB[256], lineaBLE[256]; int nUSB = 0, nBLE = 0;
void caracter(char c, char *linea, int &n, int origen, unsigned long ahora) {
  if (c == '\n' || c == '\r') { if (n > 0) { linea[n] = 0; n = 0; ejecutarLinea(linea, origen, ahora); } return; }
  if (n == 0 && strchr("ack+-<>nmprvuih", c)) { ejecutarLetra(c, origen, ahora); return; }   // una letra: de inmediato
  if (n < 255) linea[n++] = c;
}
void comandos(unsigned long ahora) {
  while (Serial.available()) caracter((char)Serial.read(), lineaUSB, nUSB, A_USB, ahora);
  char c; while (xQueueReceive(colaBLE, &c, 0) == pdTRUE) caracter(c, lineaBLE, nBLE, A_BLE, ahora);
}

// ------------------------------------------------------------------ arranque
void setup() {
  Serial.begin(115200); pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, HIGH); analogReadResolution(12);
  mem.begin("hande", false);
  sens = constrain(mem.getFloat("sens", 0.25f), 0.12f, 0.45f); vel = constrain(mem.getFloat("vel", 1.0f), 0.5f, 1.5f);
  if (mem.getBytesLength("uso") == sizeof(Uso)) mem.getBytes("uso", &uso, sizeof(Uso));
  uso.encendidos++; guardarUso();
  cargarModelo();
  ESP32PWM::allocateTimer(0); ESP32PWM::allocateTimer(1); ESP32PWM::allocateTimer(2);
  sMano.setPeriodHertz(50); sMano.attach(PIN_MANO, 500, 2500);
  sPulgar.setPeriodHertz(50); sPulgar.attach(PIN_PULGAR, 500, 2500);
  sMuneca.setPeriodHertz(50); sMuneca.attach(PIN_MUNECA, 500, 2500);
  mover(); escribirMuneca();
  iniciarBLE();
  delay(800); calibrar(); ayuda(); tUltimaActividad = millis(); tAbierta = millis();
}

// ------------------------------------------------------------------ control (1000 veces por segundo)
void loop() {
  static unsigned long tPrev = micros(), tPlot = 0, tBat = 0, tTel = 0, tEst = 0, tMin = 0, tGuardar = 0, tMTU = 0;
  unsigned long ahora = millis();
  if (micros() - tPrev < 1000) return;
  float dt = (micros() - tPrev) / 1e6f; tPrev = micros();
  comandos(ahora);
  if (reanunciar) {                                            // se fue la app: vuelve a anunciarse
    reanunciar = false; streamBLE = false; bleMTU = 23;
    if (registro && registroOrigen == A_BLE) { registro = false; enviar(A_USB, "REG,0\n"); }
    BLEDevice::startAdvertising();
  }
  if (bleConectado && ahora - tMTU > 1000) { tMTU = ahora; uint16_t m = servidor->getPeerMTU(servidor->getConnId()); if (m >= 23) bleMTU = m; }
  if (registro && ahora - tRegistro > T_REGISTRO) { registro = false; enviar(A_TODOS, "REG,0\n"); }

  env = envolvente(leerEMG());
  float nivel = constrain((env - umbralOff) / max(1.0f, maximo - umbralOff), 0.0f, 1.0f);
  bool puedeMover = !registro && estadoBat < 2;

  // ---- empieza una contracción: se guarda cómo estaba todo por si resulta ser un pulso
  if (!activo && env > umbralOn) {
    activo = true; seMovio = false; tInicio = ahora; evPico = 0; evArea = 0; evSuma = 0; evN = 0; evTPico = 0;
    manoInicio = angMano; giroInicio = giro; agarradoInicio = agarrado; munecaInicio = modoMuneca;
  }
  // ---- mientras dura: se mueve desde los 90 ms (despacio mientras todavía puede ser un pulso corto)
  if (activo) {
    evN++; evSuma += env; evArea += env * dt; if (env > evPico) { evPico = env; evTPico = ahora - tInicio; }
    tUltimaActividad = ahora;
    unsigned long t = ahora - tInicio;
    if (t > T_ARRANQUE && puedeMover) {
      float f = ((t < T_CORTO) ? ARRANQUE : 1.0f) * vel;
      if (!seMovio) { seMovio = true; enviar(A_TODOS, "LAT,%lu\n", t); }
      if (munecaInicio || agarradoInicio) { giro += sentidoGiro * VEL_MUNECA * nivel * f * dt; escribirMuneca(); }
      else {
        angMano = min(MANO_CERRADA, angMano + VEL_MANO * nivel * f * dt);
        if (angMano >= MANO_CERRADA) agarrado = true;
        aliviado = false; ajustePulgar = 0; mover();
      }
      tUltimoMov = ahora;
    }
  }
  // ---- termina la contracción: se decide qué fue y se avisa a la app ANTES de actuar
  if (activo && env < umbralOff) {
    activo = false; unsigned long d = ahora - tInicio; float x[5]; rasgos(d, x);
    float s = puntaje(x); bool intencion = s > 0;
    enviar(A_TODOS, "EV,%lu,%.4f,%.4f,%.4f,%.4f,%.4f\n", d, x[0], x[1], x[2], x[3], x[4]);
    int accion = 0;   // 0 nada · 1 abre · 2 modo muñeca · 3 vuelve a la mano · 4 ignorado · 5 cerró · 6 giró · 7 sin batería · 8 registro
    if (registro) accion = 8;
    else if (d <= T_CORTO) {
      angMano = manoInicio; agarrado = agarradoInicio; giro = giroInicio; mover(); escribirMuneca();   // se deshace el pulso
      if (!intencion) accion = 4;
      else if (munecaInicio) accion = 3;
      else if (agarradoInicio || manoInicio > MANO_REPOSO + 1.0f) accion = 1;
      else accion = (estadoBat >= 2) ? 7 : 2;
    } else if (munecaInicio || agarradoInicio) { sentidoGiro = -sentidoGiro; accion = seMovio ? 6 : 0; }   // la próxima vez al otro lado
    else accion = (angMano > manoInicio + 5.0f) ? 5 : 0;
    static const char *nombres[] = {"nada", "abre", "muneca", "mano", "ignorado", "cierra", "gira", "sin_bateria", "registro"};
    enviar(A_TODOS, "DEC,%lu,%d,%.2f,%s\n", d, intencion ? 1 : 0, s, nombres[accion]);
    switch (accion) {
      case 1: abrir(); uso.aperturas++; break;
      case 2: ponerModoMuneca(true); break;
      case 3: ponerModoMuneca(false); break;
      case 4: uso.ignorados++; break;
      case 5: uso.cierres++; break;
      case 6: uso.giros++; break;
      default: break;
    }
    if (accion >= 1 && accion <= 6) usoCambio = true;
  }
  // ---- en reposo
  if (!activo) {
    if (modoMuneca && ahora - tUltimoMov > T_MODO) ponerModoMuneca(false);           // regresa sola a la mano
    if (!registro && !modoMuneca && !agarrado && aliviado && angMano <= MANO_ABIERTA + 0.5f && ahora - tAbierta > T_REPOSO) { angMano = MANO_REPOSO; mover(); }
    if (!aliviado && ahora - tUltimoMov > T_ALIVIO) {        // ya agarró: los servos de la mano aflojan un poco (no se calientan)
      angMano = constrain(angMano - ALIVIO, MANO_ABIERTA, MANO_CERRADA); sMano.write((int)round(angMano));
      float p = pulgarPara(angMano) + ajustePulgar; p += (PUL_C > PUL_A ? -ALIVIO_PULGAR : ALIVIO_PULGAR);
      sPulgar.write((int)round(constrain(p, min(PUL_A, PUL_C), max(PUL_A, PUL_C)))); aliviado = true;
    }
    if (ahora - tUltimaActividad > 2000) {                   // umbrales adaptativos: siguen al nivel de reposo de hoy
      umbralOff += 0.0005f * ((env + max(0.05f, sens - 0.10f) * maximo) - umbralOff); umbralOn = umbralOff + 0.10f * maximo;
    }
  }
  // ---- batería (promediada: una bajada al mover los motores no cuenta)
  if (ahora - tBat > 1000) {
    tBat = ahora; float v = bateria();
    if (v > 3.0f) {
      vBat = (vBat < 1.0f) ? v : vBat + 0.2f * (v - vBat);
      int e = (vBat < BAT_CORTE) ? 2 : (vBat < BAT_AVISO) ? 1 : 0;
      if (e > estadoBat) {
        estadoBat = e; enviar(A_TODOS, "BAT,%d,%.2f\n", e, vBat);
        if (e == 2) { modoMuneca = false; guardarUso(); if (!agarrado) tic(3); }
      } else if (e == 0 && estadoBat > 0 && vBat > BAT_AVISO + 0.3f) { estadoBat = 0; enviar(A_TODOS, "BAT,0,%.2f\n", vBat); }   // se cargó
    }
  }
  // ---- uso y datos en vivo
  if (ahora - tMin >= 60000) { tMin = ahora; uso.minutos++; usoCambio = true; }
  if (usoCambio && ahora - tGuardar > 300000) { tGuardar = ahora; guardarUso(); }
  if ((streamUSB || streamBLE) && ahora - tTel >= 40) {
    tTel = ahora; int a = (streamUSB ? A_USB : 0) | (streamBLE ? A_BLE : 0);
    int fl = (activo ? 1 : 0) | (modoMuneca ? 2 : 0) | (agarrado ? 4 : 0) | (registro ? 8 : 0) | (estadoBat == 1 ? 16 : 0) | (estadoBat == 2 ? 32 : 0);
    enviar(a, "T,%.0f,%.0f,%.0f,%.0f,%.0f,%d\n", env, umbralOn, umbralOff, angMano, giro, fl);
    if (ahora - tEst >= 1000) { tEst = ahora; enviar(a, "S,%.2f,%.0f,%d,%.2f,%.2f,%d\n", vBat, maximo, fuenteModelo, sens, vel, bleMTU); }
  }
  if (plot && ahora - tPlot > 20) { tPlot = ahora; enviar(A_USB, "emg:%.0f on:%.0f off:%.0f mano:%.0f muneca:%.0f modo:%d agarre:%d\n", env, umbralOn, umbralOff, angMano, giro, modoMuneca ? 1 : 0, agarrado ? 1 : 0); }
}
