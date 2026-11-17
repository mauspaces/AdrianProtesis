# Prótesis izquierda de Adrián · manual de armado (HAND-E · Nacional InnovaTecNM 2026)

Sitio estático (GitHub Pages): modelo 3D interactivo, mano con **agarre adaptativo** (un servo + diferencial de
balancines: cada dedo se detiene al tocar el objeto), muñeca con giro natural de ±80°, sensor EMG en el socket,
qué es cada pieza (material, medidas, filamento), cómo se une el socket con el antebrazo, manual de armado paso a paso,
capacidad estimada, recomendaciones y descarga de cada pieza en STL (41 piezas en PETG; todas caben en una impresora de
22 cm, la más grande mide 158 mm). Sin piezas de acero compradas: todo con tornillos M2/M2.5/M3.

- `index.html`, `app.js` — la página (three.js r128 local).
- `modelo.js` — mallas reducidas, catálogo, pasos y datos del mecanismo (generado por `scripts/48_sitio_manual.py`).
- `stl/` — cada pieza para imprimir, en milímetros, y un ZIP con todas.
- `firmware/protesis_adrian.ino` — programa para la ESP32-S3 (calibración personalizada con umbrales adaptativos).

Los escaneos se publican con permiso de Adrián. Las capacidades (agarre, carga, autonomía) son estimaciones de
cálculo, no pruebas.
