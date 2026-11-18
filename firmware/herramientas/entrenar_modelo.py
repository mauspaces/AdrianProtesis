# -*- coding: utf-8 -*-
"""
Hand-e · entrena el modelo que decide si un pulso corto fue A PROPÓSITO o un movimiento del brazo sin querer.
Aprendizaje automático con las señales de Adrián: análisis discriminante lineal (LDA) de Fisher sobre 5 rasgos de
cada contracción (duración, pico, área, tiempo al pico y forma), los mismos que calcula el firmware v8.
Es la versión para computadora de la pestaña «Entrenar» de Hand-e Studio (studio.html): mismos rasgos, misma cuenta.

Uso
  1) Carga firmware/protesis_adrian en la ESP32 y ponle el electrodo a Adrián (no hace falta la prótesis armada).
     Deja que termine la calibración del arranque (relaja 3 s; al primer tic, aprieta 3 s; dos tics = lista).
  2) python entrenar_modelo.py COM5          (Windows; en Mac/Linux algo como /dev/ttyACM0)
     Pide 20 pulsos cortos a propósito, 20 movimientos del brazo SIN contraer y 10 contracciones sostenidas.
     En modo entrenamiento la mano NO se mueve.
  3) Guarda los datos (CSV), entrena, compara contra la regla fija, escribe modelo_emg.h (respaldo) y SUBE el
     modelo a la memoria de la ESP32 por el mismo cable: queda activo al instante, sin volver a compilar.
  Reentrenar con datos ya guardados:  python entrenar_modelo.py --csv datos_adrian_AAAAMMDD_HHMM.csv [--subir COM5]
Requiere: pip install pyserial numpy matplotlib   (matplotlib es opcional: solo para la gráfica)
"""
import argparse, csv, datetime, pathlib, sys, time
import numpy as np

AQUI = pathlib.Path(__file__).resolve().parent
PROGRAMA = AQUI.parent / 'protesis_adrian'
T_CORTO = 0.350                                     # s: igual que T_CORTO del firmware
RASGOS = ['duracion_s', 'pico', 'area', 'subida_s', 'forma']
PLAN = [('corta', 20, 'Haz un PULSO CORTO a propósito (como para abrir la mano)'),
        ('movimiento', 15, 'Mueve el brazo de golpe SIN contraer: levántalo rápido, gíralo o da un golpecito al socket'),
        ('sostenida', 10, 'APRIETA y SOSTÉN como 1 segundo, luego suelta')]
# Un movimiento que ni activa el sensor está bien, pero no enseña nada: se repite hasta registrar 15 (máximo 45 intentos).


def regla_fija(x):                                  # la regla del firmware sin modelo
    return x[0] >= 0.08 and x[1] >= 0.35


def abrir_puerto(puerto):
    try:
        import serial
    except ImportError:
        sys.exit('Falta pyserial:  pip install pyserial')
    sp = serial.Serial(); sp.port = puerto; sp.baudrate = 115200; sp.timeout = 0.2
    sp.dtr = False; sp.rts = False                  # así abrir el puerto no reinicia la ESP32
    sp.open(); time.sleep(1.5); sp.reset_input_buffer()
    return sp


def esperar(sp, prefijo, segundos):
    t0 = time.time()
    while time.time() - t0 < segundos:
        l = sp.readline().decode(errors='ignore').strip()
        if l.startswith(prefijo):
            return l
    return None


# ------------------------------------------------------------------ registro por el puerto serie
def registrar(sp, carpeta):
    sp.write(b'r\n'); cal = esperar(sp, 'CAL,', 3)            # el firmware responde REG,1 y CAL,base,maximo,umbral,...
    if cal is None:
        sys.exit('La ESP32 no respondió. ¿Es el puerto correcto y ya terminó la calibración del arranque?')
    print('Calibración de la ESP32:', cal)
    filas, sin_evento = [], {c: 0 for c, _, _ in PLAN}
    for clase, n, instruccion in PLAN:
        print(f'\n=== {instruccion} ({n} veces). Espera a que te diga «ahora» cada vez. ===')
        i = reg = 0
        while (reg < n and i < 3 * n) if clase == 'movimiento' else i < n:
            i += 1
            input(f'  [{reg if clase == "movimiento" else i}/{n}] Presiona Enter, luego hazlo... ')
            print('  ahora'); sp.reset_input_buffer()
            l = esperar(sp, 'EV,', 4.0)
            if l is None:
                sin_evento[clase] += 1
                print('  (no se detectó contracción: bien si era un movimiento, mal si era a propósito)')
            else:
                ev = [float(v) for v in l.split(',')[1:]]
                filas.append([clase] + ev[1:]); reg += 1; print(f'  registrado: {ev[0]:.0f} ms, pico {100 * ev[2]:.0f} % de su fuerza')
            time.sleep(0.8)
    sp.write(b'r\n')                                          # sale del modo entrenamiento
    archivo = carpeta / f'datos_adrian_{datetime.datetime.now():%Y%m%d_%H%M}.csv'
    with open(archivo, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f); w.writerow(['clase'] + RASGOS); w.writerows(filas)
        for c, k in sin_evento.items():                            # los no detectados también cuentan para el reporte
            for _ in range(k):
                w.writerow([c] + [''] * len(RASGOS))
    print('\nDatos guardados en', archivo)
    return archivo


# ------------------------------------------------------------------ entrenamiento (LDA de Fisher, sin librerías raras)
def leer(archivo):
    eventos, faltan = [], {}
    with open(archivo, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['duracion_s'] == '':
                faltan[r['clase']] = faltan.get(r['clase'], 0) + 1
            else:
                eventos.append((r['clase'], np.array([float(r[k]) for k in RASGOS])))
    return eventos, faltan


def lda(X, y, lam=0.05):
    media, escala = X.mean(0), X.std(0) + 1e-9
    Z = (X - media) / escala
    m1, m0 = Z[y == 1].mean(0), Z[y == 0].mean(0)
    S = np.cov(Z[y == 1].T) * (np.sum(y == 1) - 1) + np.cov(Z[y == 0].T) * (np.sum(y == 0) - 1)
    S = S / max(1, len(y) - 2) + lam * np.eye(X.shape[1])
    w = np.linalg.solve(S, m1 - m0); b = -w @ (m1 + m0) / 2
    return dict(media=media, escala=escala, w=w, b=b)


def decide(m, x):
    return (m['w'] @ ((x - m['media']) / m['escala']) + m['b']) > 0


def entrenar(archivo):
    eventos, faltan = leer(archivo)
    cortos = [(c, x) for c, x in eventos if x[0] <= T_CORTO and c in ('corta', 'movimiento')]
    n1 = sum(c == 'corta' for c, _ in cortos); n0 = sum(c == 'movimiento' for c, _ in cortos)
    if n1 < 5 or n0 < 5:
        sys.exit(f'Muy pocos pulsos para entrenar ({n1} a propósito, {n0} movimientos). Repite el registro.')
    X = np.array([x for _, x in cortos]); y = np.array([1 if c == 'corta' else 0 for c, _ in cortos])
    modelo = lda(X, y)
    # validación dejando uno fuera: cada pulso se juzga con un modelo que nunca lo vio
    aciertos_m = sum(decide(lda(np.delete(X, i, 0), np.delete(y, i)), X[i]) == bool(y[i]) for i in range(len(y)))
    aciertos_r = sum(regla_fija(X[i]) == bool(y[i]) for i in range(len(y)))
    acc_m, acc_r = round(100 * aciertos_m / len(y)), round(100 * aciertos_r / len(y))
    # reporte de control completo, por gesto (incluye lo que ni se detectó y lo que duró demasiado)
    rep = []
    for clase, _, _ in PLAN:
        evs = [x for c, x in eventos if c == clase]; nd = faltan.get(clase, 0); total = len(evs) + nd
        if clase == 'corta':
            ok_r = sum(x[0] <= T_CORTO and regla_fija(x) for x in evs)
            ok_m = sum(x[0] <= T_CORTO and decide(modelo, x) for x in evs)
        elif clase == 'movimiento':                             # bien = no hace nada (ni se detecta, o se ignora)
            ok_r = nd + sum(x[0] <= T_CORTO and not regla_fija(x) for x in evs)
            ok_m = nd + sum(x[0] <= T_CORTO and not decide(modelo, x) for x in evs)
        else:                                                   # sostenida: bien = duró más que un pulso
            ok_r = ok_m = sum(x[0] > T_CORTO for x in evs)
        rep.append((clase, total, ok_r, ok_m))
    tot = sum(r[1] for r in rep); ok_r = sum(r[2] for r in rep); ok_m = sum(r[3] for r in rep)
    print('\nResultados con las señales registradas')
    print(f'  Pulsos cortos ({len(y)}): modelo {acc_m} % (validación dejando uno fuera), regla fija {acc_r} %')
    for clase, total, a, b in rep:
        print(f'  {clase:11s} {total:3d} intentos · regla fija {a:3d} bien · modelo {b:3d} bien')
    print(f'  TOTAL       {tot:3d} intentos · regla fija {100 * ok_r / tot:.0f} % · modelo {100 * ok_m / tot:.0f} %')
    # modelo_emg.h (respaldo: el firmware lo usa solo si en la memoria no hay un modelo subido)
    fecha = f'{datetime.datetime.now():%Y-%m-%d %H:%M}'
    f = lambda v: ', '.join(f'{float(a):.6f}f' for a in v)
    h = (f'// modelo_emg.h — generado por entrenar_modelo.py el {fecha} con {n1} pulsos a propósito y {n0} movimientos sin querer\n'
         f'// Validación dejando uno fuera: modelo {acc_m} %, regla fija {acc_r} %. Datos: {pathlib.Path(archivo).name}\n'
         '#pragma once\nconst int MODELO_N = 5;\n'
         f'const float MODELO_MEDIA[5] = {{{f(modelo["media"])}}};\nconst float MODELO_ESCALA[5] = {{{f(modelo["escala"])}}};\n'
         f'const float MODELO_W[5] = {{{f(modelo["w"])}}};\nconst float MODELO_B = {float(modelo["b"]):.6f}f;\n')
    destino = PROGRAMA / 'modelo_emg.h'; destino.write_text(h, encoding='utf-8')
    print('\nRespaldo escrito en', destino)
    resumen = pathlib.Path(archivo).with_suffix('.txt')
    resumen.write_text('\n'.join([f'Hand-e · entrenamiento {fecha} · datos {pathlib.Path(archivo).name}',
                                  f'Pulsos cortos ({len(y)}): modelo {acc_m} %, regla fija {acc_r} %'] +
                                 [f'{c}: {t} intentos, regla fija {a} bien, modelo {b} bien' for c, t, a, b in rep] +
                                 [f'TOTAL: regla fija {100 * ok_r / tot:.0f} %, modelo {100 * ok_m / tot:.0f} %']), encoding='utf-8')
    try:                                                         # gráfica para la evidencia (opcional)
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(7, 5), dpi=130)
        for c, col, et in (('corta', '#2F6FE0', 'Pulso a propósito'), ('movimiento', '#8C93A1', 'Movimiento sin querer'), ('sostenida', '#1E9E7A', 'Sostenida')):
            pts = np.array([x for cc, x in eventos if cc == c])
            if len(pts):
                ax.scatter(pts[:, 0] * 1000, pts[:, 1] * 100, s=28, c=col, label=et, alpha=0.85)
        ax.axvline(T_CORTO * 1000, color='#C2412D', ls='--', lw=1); ax.text(T_CORTO * 1000 + 8, 2, 'límite de pulso 350 ms', color='#C2412D', fontsize=8)
        ax.axhline(35, color='#5B6B86', ls=':', lw=1); ax.text(5, 37, 'regla fija: 35 % de fuerza', color='#5B6B86', fontsize=8)
        ax.set_xlabel('Duración de la contracción (ms)'); ax.set_ylabel('Pico (% de su fuerza máxima)')
        ax.set_title(f'Contracciones registradas · modelo {acc_m} % vs regla fija {acc_r} % en pulsos'); ax.legend(fontsize=8)
        fig.tight_layout(); fig.savefig(pathlib.Path(archivo).with_suffix('.png')); print('Gráfica:', pathlib.Path(archivo).with_suffix('.png'))
    except ImportError:
        pass
    return modelo, n1, n0, acc_m, acc_r


def linea_modelo(modelo, n1, n0, acc_m, acc_r):                 # el mismo formato que manda Hand-e Studio
    v = list(modelo['media']) + list(modelo['escala']) + list(modelo['w']) + [modelo['b']]
    return 'M,' + ','.join(f'{float(a):.6f}' for a in v) + f',{n1},{n0},{acc_m},{acc_r}\n'


def subir(sp, *datos):
    sp.reset_input_buffer(); sp.write(linea_modelo(*datos).encode())
    if esperar(sp, 'OK,modelo', 3):
        print('Modelo guardado en la memoria de la prótesis: ya está activo (no hay que volver a compilar).')
    else:
        print('La ESP32 no confirmó el modelo. Usa el respaldo: vuelve a cargar el programa con modelo_emg.h en la carpeta.')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Entrena el modelo de Hand-e con las señales de Adrián')
    ap.add_argument('puerto', nargs='?', help='puerto de la ESP32 (COM5, /dev/ttyACM0...)')
    ap.add_argument('--csv', help='reentrenar con un CSV ya guardado')
    ap.add_argument('--subir', metavar='PUERTO', help='con --csv: sube el modelo a la ESP32 conectada en ese puerto')
    a = ap.parse_args()
    if a.csv:
        datos = entrenar(a.csv)
        if a.subir:
            sp = abrir_puerto(a.subir); subir(sp, *datos); sp.close()
    elif a.puerto:
        sp = abrir_puerto(a.puerto)
        datos = entrenar(registrar(sp, AQUI))
        subir(sp, *datos); sp.close()
    else:
        ap.print_help()
