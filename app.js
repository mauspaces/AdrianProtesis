/* Prótesis izquierda de Adrián · sitio del manual (three.js r128) */
(function () {
  'use strict';
  const M = window.MODELO, mov = M.mov, $ = id => document.getElementById(id);
  const V3 = a => new THREE.Vector3().fromArray(a);
  const b64 = (s, T) => { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return new T(u.buffer); };
  const interp = (x, xs, ys) => { if (x <= xs[0]) return ys[0]; for (let i = 1; i < xs.length; i++) if (x <= xs[i]) { const f = (x - xs[i - 1]) / (xs[i] - xs[i - 1]); return ys[i - 1] + f * (ys[i] - ys[i - 1]); } return ys[ys.length - 1]; };
  const sm = x => { x = Math.min(1, Math.max(0, x)); return x * x * (3 - 2 * x); };
  const reducido = matchMedia('(prefers-reduced-motion: reduce)').matches;

  // ---------------------------------------------------------------- geometrias (una sola vez, compartidas)
  const GEO = {};
  function geo(clave, obj) {
    if (GEO[clave]) return GEO[clave];
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(b64(obj.v, Float32Array), 3)); g.setIndex(new THREE.BufferAttribute(b64(obj.f, Uint32Array), 1));
    g.computeVertexNormals(); g.computeBoundingSphere(); return (GEO[clave] = g);
  }
  const MAT = {};
  function mat(color, extra) {
    const k = color + JSON.stringify(extra || {}); if (MAT[k] && !(extra && extra.unico)) return MAT[k];
    const m = new THREE.MeshStandardMaterial(Object.assign({ color, roughness: 0.55, metalness: 0.05, side: THREE.DoubleSide }, extra || {}));
    delete m.unico; return (MAT[k] = m);
  }
  const metal = c => /^#(b9c1cc|3b4049|8d949e|6b727c)$/.test(c);

  // ---------------------------------------------------------------- visor base
  function Visor(canvas, op) {
    op = op || {};
    const r = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: !!op.captura, alpha: false });
    r.setPixelRatio(Math.min(devicePixelRatio, 2)); r.setClearColor(op.fondo == null ? 0x000000 : op.fondo); r.localClippingEnabled = true;
    const escena = new THREE.Scene(), cam = new THREE.PerspectiveCamera(op.fov || 28, 1, 1, 6000);
    escena.add(new THREE.HemisphereLight(0xffffff, 0x50555c, 0.8));
    const l1 = new THREE.DirectionalLight(0xffffff, 0.75), l2 = new THREE.DirectionalLight(0xffffff, 0.3); escena.add(l1, l2);
    const v = { r, escena, cam, centro: new THREE.Vector3(), az: op.az ?? -2.2, el: op.el ?? 0.45, dist: op.dist ?? 700, destino: null, auto: op.auto || 0, vivo: true, cada: null };
    v.colocar = () => {
      v.el = Math.max(-1.5, Math.min(1.5, v.el)); const ce = Math.cos(v.el), se = Math.sin(v.el);
      cam.position.set(v.centro.x + v.dist * ce * Math.cos(v.az), v.centro.y + v.dist * ce * Math.sin(v.az), v.centro.z + v.dist * se);
      cam.up.set(0, 0, 1); cam.lookAt(v.centro);
      l1.position.copy(cam.position).add(new THREE.Vector3(0, 0, 500)); l2.position.set(2 * v.centro.x - cam.position.x, 2 * v.centro.y - cam.position.y, v.centro.z - 400);
    };
    v.tam = () => { const w = canvas.clientWidth, h = canvas.clientHeight; if (!w || !h) return; r.setSize(w, h, false); cam.aspect = w / h; cam.updateProjectionMatrix(); };
    v.ir = (az, el, dist, centro) => { v.destino = { az, el, dist: dist ?? v.dist, centro: centro ? centro.clone() : v.centro.clone() }; };
    v.encuadrar = (obj, factor) => { const b = new THREE.Box3().setFromObject(obj); if (b.isEmpty()) return; const s = b.getBoundingSphere(new THREE.Sphere()); v.ir(v.az, v.el, s.radius * (factor || 3.4), s.center); };
    if (!op.quieto) {
      let arr = null;
      canvas.addEventListener('pointerdown', e => { arr = { x: e.clientX, y: e.clientY, mover: e.button === 2 || e.shiftKey }; v.destino = null; v.auto = 0; canvas.setPointerCapture(e.pointerId); });
      canvas.addEventListener('pointermove', e => {
        if (!arr) return; const dx = e.clientX - arr.x, dy = e.clientY - arr.y; arr.x = e.clientX; arr.y = e.clientY;
        if (arr.mover) { const der = new THREE.Vector3().subVectors(cam.position, v.centro).cross(cam.up).normalize(), k = v.dist / canvas.clientHeight * 0.55; v.centro.addScaledVector(der, -dx * k).addScaledVector(cam.up, dy * k); }
        else { v.az -= dx * 0.006; v.el += dy * 0.006; }
      });
      canvas.addEventListener('pointerup', () => arr = null); canvas.addEventListener('contextmenu', e => e.preventDefault());
      canvas.addEventListener('wheel', e => { if (!op.rueda) return; e.preventDefault(); v.destino = null; v.dist = Math.max(80, Math.min(2500, v.dist * Math.exp(e.deltaY * 0.001))); }, { passive: false });
    }
    new IntersectionObserver(es => es.forEach(x => v.vivo = x.isIntersecting), { rootMargin: '120px' }).observe(canvas);
    addEventListener('resize', v.tam); v.tam();
    let t0 = performance.now();
    function cuadro(t) {
      const dt = Math.min(0.05, (t - t0) / 1000); t0 = t;
      if (v.vivo) {
        if (v.auto && !reducido) v.az += dt * v.auto;
        if (v.destino) { const k = 1 - Math.exp(-dt * 5), D = v.destino; let da = ((D.az - v.az + Math.PI) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI) - Math.PI;
          v.az += da * k; v.el += (D.el - v.el) * k; v.dist += (D.dist - v.dist) * k; v.centro.lerp(D.centro, k); if (Math.abs(da) < 1e-3 && Math.abs(D.el - v.el) < 1e-3 && Math.abs(D.dist - v.dist) < 0.5) v.destino = null; }
        if (v.cada) v.cada(dt); v.colocar(); r.render(escena, cam);
      }
      requestAnimationFrame(cuadro);
    }
    requestAnimationFrame(cuadro);
    return v;
  }

  // ---------------------------------------------------------------- protesis armada con sus articulaciones
  function Protesis(escena, op) {
    op = op || {};
    const piv = V3(mov.piv), ejeCodo = V3(mov.eje).normalize(), wc = V3(mov.giro.eje_punto), dirM = V3(mov.d).normalize(), ejeG = V3(mov.giro.eje_dir).normalize(), ejeA = V3(mov.eje_comun.dir).normalize();
    const codo = new THREE.Group(); codo.position.copy(piv); escena.add(codo);
    const prot = new THREE.Group(); prot.position.copy(piv).multiplyScalar(-1); codo.add(prot);
    const muneca = new THREE.Group(); muneca.position.copy(wc); prot.add(muneca);
    const mano = new THREE.Group(); mano.position.copy(wc).multiplyScalar(-1); muneca.add(mano);
    const J = mov.orden.map(n => mov.dedos[n].map(j => ({ p: V3(j.p), e: V3(j.eje).normalize() })));
    const G = {}, eoP = mov.pulgar ? V3(mov.pulgar.eje).normalize() : null;
    mov.orden.forEach((n, k) => {
      const Jk = J[k];
      if (n === 'pulgar') { const g0 = new THREE.Group(); g0.position.copy(Jk[0].p); mano.add(g0); G.pulgar = { pulgar: [g0, Jk[0].p], g0 }; return; }
      const q = V3(mov.eje_comun.puntos[n]), g0 = new THREE.Group(); g0.position.copy(q); mano.add(g0);
      const g1 = new THREE.Group(); g1.position.copy(Jk[1].p).sub(q); g0.add(g1);
      const g2 = new THREE.Group(); g2.position.copy(Jk[2].p).sub(Jk[1].p); g1.add(g2);
      G[n] = { B1: [g0, q], B2: [g1, Jk[1].p], B3: [g2, Jk[2].p], g0, g1, g2, q };
    });
    const dif = new THREE.Group(); mano.add(dif);                         // carro + balancines: se deslizan al cerrar
    const padre = (g, d) => g === 'dif' ? [dif, null] : g === 'brazo' ? [prot, null] : g === 'muneca' ? [muneca, wc] : g === 'palma' ? [mano, null] : g === 'pulgar' ? G.pulgar.pulgar : G[d] ? G[d][g] : [prot, null];
    const todas = [], porId = {}, bielasImp = [], guante = [];
    function poner(clave, o, material, lista) {
      const [pa, org] = padre(o.grupo, o.dedo), ms = new THREE.Mesh(geo(clave, o), material);
      if (org) ms.position.copy(org).multiplyScalar(-1); pa.add(ms);
      ms.userData.base = ms.position.clone(); ms.userData.dir = V3(o.explo || [0, 0, 0]); ms.userData.clave = clave; lista && lista.push(ms); todas.push(ms); return ms;
    }
    Object.entries(M.piezas).forEach(([k, o]) => {
      const ms = poner(k, o, mat(o.color, metal(o.color) ? { metalness: 0.7, roughness: 0.3 } : {}));
      porId[k] = ms; if (/^P(29|3\d|4[01])$/.test(k)) bielasImp.push(ms);
    });
    Object.entries(M.guante).forEach(([k, o]) => { const ms = poner('g:' + k, o, mat('#a9744f', { roughness: 0.8 }), guante); ms.visible = false; });
    // bielas en movimiento (barras entre sus dos pernos)
    const mB = M.mov.mecB, DF = M.mov.dif, mBar = mat('#c9a35a', { metalness: 0.3, roughness: 0.4 }), barras = [], gBar = new THREE.CylinderGeometry(1.3, 1.3, 1, 10);
    const marca = (pa, p, org) => { const o = new THREE.Object3D(); o.position.copy(V3(p)); if (org) o.position.sub(org); pa.add(o); return o; };
    const pares = [];
    ['indice', 'medio', 'anular', 'menique'].forEach(n => { const d = mB.dedos[n], g = G[n]; pares.push([marca(mano, d.G), marca(g.g1, d.C, J[mov.orden.indexOf(n)][1].p)], [marca(g.g0, d.E, g.q), marca(g.g2, d.F, J[mov.orden.indexOf(n)][2].p)]); });
    ['indice', 'medio', 'anular', 'menique'].forEach(n => pares.push([marca(G[n].g0, DF.L0[n], G[n].q), marca(dif, DF.B0[n])]));     // bieletas: palanca -> balancín
    const Hm = new THREE.Object3D(); mano.add(Hm); pares.push([Hm, marca(dif, DF.Q0)]);                                          // biela del cuerno
    const Hp = new THREE.Object3D(); mano.add(Hp); if (mov.pulgar && mov.pulgar.E0) pares.push([Hp, marca(G.pulgar.g0, mov.pulgar.E0, J[mov.orden.indexOf('pulgar')][0].p)]);   // varilla del pulgar (clip)
    const gB = new THREE.Group(); mano.add(gB);
    pares.forEach(() => { const b = new THREE.Mesh(gBar, mBar); gB.add(b); barras.push(b); });
    const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _y = new THREE.Vector3(0, 1, 0);
    function bielas() {
      mano.updateMatrixWorld(true);
      pares.forEach(([a, b], i) => { a.getWorldPosition(_a); b.getWorldPosition(_b); mano.worldToLocal(_a); mano.worldToLocal(_b);
        const L = _a.distanceTo(_b), br = barras[i]; br.position.copy(_a).add(_b).multiplyScalar(0.5); br.scale.set(1, L, 1); br.quaternion.setFromUnitVectors(_y, _b.clone().sub(_a).normalize()); });
    }
    // referencias: muñon y brazo derecho (se doblan en el codo)
    const refs = {};
    Object.entries(M.refs).forEach(([k, o]) => {
      const g = geo('r:' + k, o).clone(), ms = new THREE.Mesh(g, mat(k === 'munon' ? '#c97a64' : '#cdb39d', { roughness: 0.7 })); escena.add(ms);
      const p = g.attributes.position.array, w = new Float32Array(p.length / 3), sh = k === 'ref' || k === 'brazo_derecho' ? 2 * piv.x - 150 : 0;
      for (let i = 0; i < w.length; i++) { const s = (p[3 * i] - piv.x) * dirM.x + (p[3 * i + 1] - piv.y) * dirM.y + (p[3 * i + 2] - piv.z) * dirM.z; w[i] = sm((s + 20) / 40); }
      refs[k] = { ms, p0: p.slice(), w, sh };
      if (k === 'brazo_derecho') { ms.position.x = sh; ms.visible = false; }
    });
    function doblarRef(k, ang) {
      const R = refs[k]; if (!R) return; const a = R.ms.geometry.attributes.position, p = a.array, q = new THREE.Quaternion(), v = new THREE.Vector3();
      const pc = k === 'brazo_derecho' ? new THREE.Vector3(-piv.x, piv.y, piv.z) : piv, ej = k === 'brazo_derecho' ? new THREE.Vector3(-ejeCodo.x, ejeCodo.y, ejeCodo.z) : ejeCodo, sgn = k === 'brazo_derecho' ? -1 : 1;
      for (let i = 0; i < R.w.length; i++) { v.set(R.p0[3 * i] - pc.x, R.p0[3 * i + 1] - pc.y, R.p0[3 * i + 2] - pc.z);
        if (ang && R.w[i] > 0) { q.setFromAxisAngle(ej, sgn * mov.sg * ang * R.w[i] * Math.PI / 180); v.applyQuaternion(q); }
        p[3 * i] = v.x + pc.x; p[3 * i + 1] = v.y + pc.y; p[3 * i + 2] = v.z + pc.z; }
      a.needsUpdate = true; R.ms.geometry.computeVertexNormals();
    }
    const est = { cierre: 0, muneca: 0, codo: 0, expl: 0, guante: false, transp: false };
    const transparentes = ['P01', 'P02', 'P03', 'P09', 'P10', 'P24'].map(k => porId[k]).filter(Boolean);
    function aplicar() {
      const r = Math.PI / 180, e = est.expl > 0.001 ? est.expl : 0, c = e ? 0 : Math.min(est.cierre, eoP ? mov.pulgar.toque_pinza : 1), t1 = c * mB.nudillo_max, pu = e ? 0 : mov.pulgar.maximo * Math.min(1, est.cierre / 0.8);   // el pulgar (MG90S) va un poco adelantado; el índice se detiene al tocarlo
      ['indice', 'medio', 'anular', 'menique'].forEach((n, k) => { const g = G[n], d = mB.dedos[n], Jk = J[k];
        g.g0.quaternion.setFromAxisAngle(ejeA, t1 * r); g.g1.quaternion.setFromAxisAngle(Jk[1].e, interp(t1, d.nudillo, d.media) * r); g.g2.quaternion.setFromAxisAngle(Jk[2].e, interp(t1, d.nudillo, d.punta) * r); });
      dif.position.copy(V3(DF.ea)).multiplyScalar(-interp(t1, DF.nudillo, DF.carro));                          // el carro va hacia la muñeca
      Hm.position.set(...[0, 1, 2].map(i => interp(t1, DF.nudillo, DF.H.map(h => h[i]))));                         // punta del cuerno
      muneca.quaternion.setFromAxisAngle(ejeG, est.muneca * r);
      codo.quaternion.setFromAxisAngle(ejeCodo, mov.sg * est.codo * r);
      if (eoP) { G.pulgar.g0.quaternion.setFromAxisAngle(eoP, pu * r); Hp.position.set(...[0, 1, 2].map(i => interp(pu, mov.pulgar.phi, mov.pulgar.H.map(h => h[i])))); }   // pulgar con motor
      todas.forEach(m => m.position.copy(m.userData.base).addScaledVector(m.userData.dir, e));
      bielasImp.forEach(m => m.visible = e > 0); gB.visible = !e && !est.guante;
      guante.forEach(m => m.visible = est.guante && !e);
      transparentes.forEach(m => { m.material = m.material.clone(); m.material.transparent = est.transp; m.material.opacity = est.transp ? 0.22 : 1; m.material.depthWrite = !est.transp; });
      doblarRef('munon', est.codo); doblarRef('brazo_derecho', est.codo); bielas();
    }
    aplicar();
    function caja(soloMano, conRefs) {           // esfera que encierra lo visible (en el mundo)
      escena.updateMatrixWorld(true); const b = new THREE.Box3().setFromObject(soloMano ? mano : prot);
      if (conRefs) Object.values(refs).forEach(R => R.ms.visible && b.expandByObject(R.ms));
      return b.getBoundingSphere(new THREE.Sphere());
    }
    return { est, aplicar, refs, todas, porId, prot, mano, wc, piv, dirM, caja };
  }

  // ---------------------------------------------------------------- datos de la pagina
  const S = M.specs, CAT = M.catalogo, porCat = {}; CAT.forEach(c => porCat[c.id] = c);
  function rail(el, filas) { $(el).innerHTML = filas.map(([n, v, u]) => `<div class="fila-spec"><span>${n}</span><b>${v}${u ? `<small> ${u}</small>` : ''}</b></div>`).join(''); }
  rail('rail', [['Codo → muñeca', S.codo_muneca_mm, 'mm'], ['Punta del muñón → dedo medio', S.punta_munon_dedo_mm, 'mm'], ['Peso estimado', '≈' + S.peso_g, 'g'],
                ['Piezas para imprimir', S.piezas_impresas, ''], ['Pieza más grande', S.max_pieza_mm, 'mm'], ['Servo para cerrar la mano', S.servo_cierre + '°', '']]);
  rail('railCap', [['Agarre máximo (servo al límite)', S.agarre_kgf, 'kgf'], ['Índice y medio, cada uno', '≈' + S.fuerza_dedo_N[0], 'N'],
                   ['Anular y meñique, cada uno', '≈' + S.fuerza_dedo_N[2], 'N'], ['Reparto del diferencial', Math.round(S.reparto.indice_medio * 100) + '/' + Math.round(S.reparto.anular_menique * 100), ''],
                   ['Giro de muñeca (desde la neutra)', '±' + S.giro.pronacion + '°', ''], ['Batería', S.autonomia_h, 'h (estimado)']]);
  $('notasCap').innerHTML = [
    '<b>Agarre:</b> alcanza para un vaso, una botella de medio litro, el celular o una bolsa ligera.',
    '<b>Gancho:</b> cargar una bolsa con los dedos cerrados. Más de ~1.5 kg hace que el servo se esfuerce y se caliente; no es para levantar peso.',
    '<b>Muñeca:</b> pasiva, se gira con la otra mano: hace clic cada 20° y topa a 80° de cada lado (como un antebrazo real). Sostiene ≈' + (S.clics ? S.clics.par_Nm : 0.4) + ' N·m antes de saltar de clic.',
    '<b>No es para</b> apoyarse, empujar el cuerpo o golpes: el plástico y el socket no están hechos para eso.'].map(x => `<li>${x}</li>`).join('');
  const RECOS = {
    'Impresión': ['Todo en <b>PETG</b>: blanco para antebrazo, palma y dedos; negro (grafito) para el socket, la muñeca y el mecanismo.', 'Imprime primero <b>el índice y sus 2 bielitas</b> y ármalo: si las bisagras quedan duras o flojas, ajusta antes de imprimir todo.', 'Paredes de 4 líneas y relleno 25–35 %; <b>100 %</b> en el eje de los nudillos, el eje de la muñeca, el acople, el carro, los balancines y todas las bielas.', 'El eje de los nudillos se imprime <b>acostado sobre su cara plana</b>.', 'Socket de pie, soportes solo dentro de las alas; lija el borde.', 'Repasa barrenos con broca: Ø2.3 (M2 libre), Ø1.7 (M2 que se rosca), Ø5.4 (eje de los nudillos), Ø3.3 (M3 libre).'],
    'Mecanismo': ['El <b>diferencial</b> debe moverse solo con el dedo: si un balancín se traba, repasa su barreno con broca Ø2.3.', 'Una gota de grasa de litio en el buje de la muñeca, el canal del carro y cada tornillo-perno.', 'Las bieletas se <b>enganchan</b> (ojo en C) en la palanca de cada dedo: si quedan flojas, una gota de pegamento de contacto en el perno, no en el ojo.', 'Ajusta el pulgar desde el programa (PUL_A abierto, PUL_C cerrado): con los comandos <b>+</b> y <b>−</b> por USB ves cuánto debe cerrar.'],
    'Armado': ['Centra el servo de la mano en 0° (abierta) <b>antes</b> de poner su brazo.', 'Insertos de latón M2.5/M3 si vas a desarmar seguido (los tornillos directos en plástico se barren).', 'Pasa el cable del servo de la mano por el eje hueco <b>antes</b> de bajar el eje, y deja ~3 cm flojos para el giro de la muñeca.', 'Corta las orejas del MG995 extra (segueta fina y lija): las dos mitades de la palma lo aprietan.',],
    'Electrónica y batería': ['Batería LiPo 2S <b>con protección (BMS)</b>; nunca bajar de 6.4 V.', 'Carga con el cargador USB-C 2S; la primera vez, vigílala. No la cargues tapada o al sol.', 'Fusible o polifusible de 3 A a la salida de la batería.', 'Programa la mano: abierta = 0°, cerrada = ' + S.servo_cierre + '° (con el diferencial).', 'Aísla la tarjeta con termocontráctil o una mano de barniz: el sudor llega por el socket.'],
    'Uso y cuidado': ['Los primeros días: 15–30 min y revisar la piel. Marcas rojas que no se quitan en 20 min = hay que ajustar.', 'Calceta de 1 capa normalmente; de 3 capas si el muñón está más delgado (en la mañana, con calor, después de ejercicio).', 'No mojarla: la electrónica no es a prueba de agua.', 'Limpiar el socket por dentro con alcohol diario.', 'Revisar cada semana que no haya grietas en las bisagras ni en la unión con el socket.'],
    'Clínico': ['Que un protesista revise el ajuste del socket y la línea de recorte antes del uso diario.', 'Manga de suspensión de silicón para que no se resbale (el regional no tenía sujeción).', 'Definir con el equipo dónde va el sensor EMG (el socket nuevo aún no tiene su ventana).'],
    'Si algo falla': ['Un dedo no cierra: revisa que su bieleta esté enganchada y que su balancín gire libre.', 'Cierran índice y medio pero no anular y meñique: es normal que cierren después (el diferencial les da 15 %); si no cierran nunca, revisa la fricción de sus bisagras.', 'La muñeca no hace clic: revisa que el retén esté apretado y que las lengüetas no se hayan pegado al imprimir (sepáralas con un cúter).', 'Se calienta el servo: está cargando demasiado o el brazo del servo quedó fuera de 0°.'] };
  $('recos').innerHTML = Object.entries(RECOS).map(([t, l]) => `<div><h3>${t}</h3><ul>${l.map(x => `<li>${x}</li>`).join('')}</ul></div>`).join('');
  const PREG = [
    ['¿Qué quiere agarrar Adrián y cuánto pesa?', `Con el MG995 el agarre útil es de ≈${(S.agarre_kgf / 2).toFixed(1)} kgf. Si necesita más (cargar mochila, garrafón), hay que cambiar a un servo más fuerte o a un mecanismo de tornillo sin fin.`],
    ['¿Debe sostener sin gastar batería?', 'Hoy el servo tiene que hacer fuerza todo el tiempo que la mano agarra. Un mecanismo autobloqueante sostendría gratis, pero es más grande y más lento.'],
    ['¿El electrodo queda bien en la ventana del socket?', 'La ventana está en la cara antero-medial, unos 6 cm bajo la línea del codo (músculos que cierran la mano). Midan el electrodo con vernier: si no es de 30 × 37 mm, se ajusta la ventana.'],
    ['¿Cómo se va a sujetar?', 'Recomiendo una manga de silicón. Otras opciones: correa sobre el codo o socket de succión. Cambia el borde del socket.'],
    ['¿Su impresora mide 22 cm en las tres direcciones?', `La pieza más grande mide ${S.max_pieza_mm} mm, así que cabe. Si alguna dirección es menor, dime cuál.`],
    ['¿Alcanza el filamento?', `Se necesitan ≈${S.filamento_blanco_g} g de PETG blanco y ≈${S.filamento_negro_g} g de negro (más 20 % por soportes y pruebas).`],
    ['¿Guante cosmético real o pintura?', 'Un guante de silicón tapa las uniones y protege del agua; tiene que ser de su talla (la mano mide como la suya).'],
    ['¿Peso máximo aceptable?', `El diseño pesa ≈${S.peso_g} g. Si se siente pesada, se puede aligerar el antebrazo y la palma por dentro.`],
    ['¿Su posición neutra es “pulgar arriba” como la calculé?', `La calculé con el eje del codo del escaneo: la neutra queda a ${Math.round(S.giro.neutra_desde_modelo)}° de la postura del modelo (puede variar ±15°). Si al ponérsela la mano descansa girada, se corrige solo en el programa.`],
    ['¿Cómo se mueve el pulgar?', 'Con su propio motor: el MG90S que quedó libre de la muñeca, escondido en la palma junto a la base del pulgar, lo mueve con una varilla de clip. La misma señal del sensor cierra los dedos y el pulgar; el pulgar va un poco adelantado para que la punta del índice llegue a su punta (pinza), y se detiene solo si toca el objeto.'],
    ['¿Por qué gira la muñeca y no todo el antebrazo?', 'En el cuerpo, el radio gira alrededor del cúbito a lo largo del antebrazo. En una prótesis el socket tiene que quedarse fijo en el muñón (si girara, el electrodo se movería del músculo y el socket se aflojaría), así que el giro se pone en la muñeca, igual que en las prótesis comerciales.'],
    ['¿Le bastan clics cada 20°?', 'Es suficiente para comer, beber y escribir. Si quiere pasos más finos (10°), se agregan muescas al eje.'],
    ['¿Quién hará el ajuste clínico?', 'El socket viene del escaneo de hoy; el muñón cambia de volumen. Conviene imprimir un socket de prueba y que lo revise un protesista.']];
  $('pregs').innerHTML = PREG.map(([q, a]) => `<li><b>${q}</b><span>${a}</span></li>`).join('');
  $('listaDesc').innerHTML = CAT.filter(c => c.stl).map(c => `<div class="fila-spec" style="align-items:center"><span><span class="maq" style="color:#0c0c0c">${c.id}</span> &nbsp;${c.nombre} · ${c.material}</span><a class="descarga" href="${c.stl}" download>STL ↓</a></div>`).join('');

  // ---------------------------------------------------------------- tabla de piezas + inspector
  const TOR = { C13: 'tornillo_collarin', C14: 'tornillo_union', C15: 'tornillo_eje', C16: 'tornillo_acople' };
  const FIL = { mano: c => ['palma', 'B1', 'B2', 'B3', 'pulgar', 'dif'].includes(c.grupo) && c.id !== 'P02' || ['C01', 'C02', 'C08', 'C09', 'C10', 'C11', 'C12', 'C20'].includes(c.id),
               muneca: c => ['P02', 'P03', 'P04', 'P05', 'P08', 'C03', 'C04', 'C05', 'C06', 'C13', 'C15', 'C16'].includes(c.id), socket: c => ['P01', 'C07', 'C14', 'C18', 'C19'].includes(c.id),
               imprimir: c => c.tipo === 'imprimir', comprar: c => c.tipo === 'comprar', todas: () => true };
  let filtro = 'todas';
  function tabla() {
    $('tabla').querySelector('tbody').innerHTML = CAT.filter(FIL[filtro]).map(c => `<tr data-id="${c.id}"><td class="id">${c.id}</td>
      <td class="nom"><b>${c.nombre}</b><span>${c.que}</span></td><td class="ocultar-movil">${c.tipo === 'imprimir' ? c.material : c.material === '—' ? 'comprar' : c.material}</td>
      <td>${c.cantidad}</td><td class="ocultar-movil">${(c.medidas_mm || []).map(x => Math.round(x)).join(' × ')}</td>
      <td class="ocultar-movil">${c.tipo === 'imprimir' ? `<span class="tag ${c.cabe ? 'ok' : ''}">${c.cabe ? 'cabe' : 'no cabe'}</span>` : ''}</td>
      <td>${c.stl ? `<a class="descarga" href="${c.stl}" download onclick="event.stopPropagation()">STL ↓</a>` : ''}</td></tr>`).join('');
    $('tabla').querySelectorAll('tr[data-id]').forEach(tr => tr.onclick = () => inspeccionar(tr.dataset.id));
  }
  $('filtros').querySelectorAll('[data-f]').forEach(b => b.onclick = () => { filtro = b.dataset.f; $('filtros').querySelectorAll('.chip').forEach(x => x.classList.toggle('on', x === b)); tabla(); });
  function mallasDe(id) {
    const ks = Object.keys(M.piezas);
    if (M.piezas[id]) return [id];
    if (TOR[id]) return ks.filter(k => k.includes(TOR[id]));
    return ks.filter(k => k.startsWith(id + ':'));
  }
  const vi = Visor($('inspLienzo'), { fondo: 0xf4f4f4, auto: 0.5, dist: 200, el: 0.5 }), gi = new THREE.Group(); vi.escena.add(gi);
  function inspeccionar(id) {
    const c = porCat[id]; $('tabla').querySelectorAll('tr').forEach(tr => tr.classList.toggle('sel', tr.dataset.id === id));
    while (gi.children.length) gi.remove(gi.children[0]);
    mallasDe(id).forEach(k => { const o = M.piezas[k]; gi.add(new THREE.Mesh(geo(k, o), mat(o.color === '#2b2d33' ? '#3a3c42' : o.color, metal(o.color) ? { metalness: 0.7, roughness: 0.3 } : {}))); });
    if (gi.children.length) { const b = new THREE.Box3().setFromObject(gi), s = b.getBoundingSphere(new THREE.Sphere()); vi.centro.copy(s.center); vi.dist = s.radius * 3.6; }
    $('inspTxt').innerHTML = `<div class="eti" style="margin:0">${c.id} · ${c.tipo === 'imprimir' ? 'imprimir' : 'comprar'}</div><h3 class="edit" style="margin-top:10px">${c.nombre}</h3>
      ${c.medidas_mm && c.medidas_mm.length ? `<div class="medidas">${c.medidas_mm.map(x => Math.round(x)).join(' × ')} mm</div>` : ''}
      <p style="margin:0 0 10px">${c.que}</p>
      <p class="gris" style="margin:0 0 6px;font-size:14px">Material: <b style="color:#0c0c0c;font-weight:500">${c.material}</b> · Cantidad: <b style="color:#0c0c0c;font-weight:500">${c.cantidad}</b>${c.gramos != null ? ` · ≈${c.gramos < 1 ? '<1' : c.gramos} g` : ''}</p>
      ${c.nota ? `<p class="gris" style="font-size:14px;margin:0 0 12px">${c.nota}</p>` : ''}${c.stl ? `<a class="pill" href="${c.stl}" download>Descargar STL</a>` : ''}`;
  }
  tabla(); inspeccionar('P01');

  // ---------------------------------------------------------------- miniaturas (para el manual)
  const rThumb = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true }); rThumb.setSize(180, 180); rThumb.setClearColor(0xffffff);
  const eThumb = new THREE.Scene(); eThumb.add(new THREE.HemisphereLight(0xffffff, 0x60656c, 0.9)); const lt = new THREE.DirectionalLight(0xffffff, 0.7); eThumb.add(lt);
  const cThumb = new THREE.PerspectiveCamera(26, 1, 1, 4000), THUMB = {};
  function miniatura(ks, clave) {
    if (THUMB[clave]) return THUMB[clave];
    const g = new THREE.Group(); ks.forEach(k => { const o = M.piezas[k] || M.refs[k]; if (o) g.add(new THREE.Mesh(geo(M.piezas[k] ? k : 'r:' + k, o), mat(o.color || '#c97a64'))); }); eThumb.add(g);
    const s = new THREE.Box3().setFromObject(g).getBoundingSphere(new THREE.Sphere()), d = s.radius * 3.3;
    cThumb.position.set(s.center.x - d * 0.55, s.center.y - d * 0.55, s.center.z + d * 0.62); cThumb.up.set(0, 0, 1); cThumb.lookAt(s.center); lt.position.copy(cThumb.position);
    rThumb.render(eThumb, cThumb); eThumb.remove(g); return (THUMB[clave] = rThumb.domElement.toDataURL());
  }

  // ---------------------------------------------------------------- manual tipo lego
  const vm = Visor($('manualLienzo'), { fondo: 0xf6f6f6, dist: 600, el: 0.5, az: -2.3, rueda: true });
  const gm = new THREE.Group(); vm.escena.add(gm); const MM = {};
  Object.entries(M.piezas).forEach(([k, o]) => { const ms = new THREE.Mesh(geo(k, o), mat(o.color, metal(o.color) ? { metalness: 0.7, roughness: 0.3 } : {})); ms.userData.dir = V3(o.explo || [0, 0, 0]); ms.visible = false; gm.add(ms); MM[k] = ms; });
  Object.entries(M.refs).forEach(([k, o]) => { const ms = new THREE.Mesh(geo('r:' + k, o), mat('#c97a64', { roughness: 0.7 })); ms.visible = false; ms.userData.dir = new THREE.Vector3(); gm.add(ms); MM[k] = ms; });
  const nuevoMat = mat('#0c0c0c', { roughness: 0.45 }), viejoMat = mat('#bdbdbd', { roughness: 0.7, transparent: true, opacity: 0.2, depthWrite: false });
  const resolver = p => M.piezas[p] || M.refs[p] ? [p] : mallasDe(p);
  const PASOS = M.pasos; let paso = 0, anim = 0;
  function irPaso(i) {
    paso = Math.max(0, Math.min(PASOS.length - 1, i)); const P = PASOS[paso];
    const antes = new Set(), ahora = new Set(resolver ? P.p.flatMap(resolver) : []);
    PASOS.slice(0, paso).forEach(x => x.p.flatMap(resolver).forEach(k => antes.add(k)));
    const todo = paso === 0;
    Object.entries(MM).forEach(([k, ms]) => {
      ms.visible = todo ? (M.piezas[k] && porCat[k] && porCat[k].tipo === 'imprimir') : (antes.has(k) || ahora.has(k));
      ms.material = todo ? mat(M.piezas[k] ? M.piezas[k].color : '#ccc') : ahora.has(k) ? (k === 'munon' ? mat('#c97a64') : nuevoMat) : viejoMat;
      ms.position.set(0, 0, 0); ms.userData.nuevo = !todo && ahora.has(k);
      if (todo) ms.position.copy(ms.userData.dir);
    });
    anim = 0;
    const foco = new THREE.Group(), ks = todo ? Object.keys(MM).filter(k => MM[k].visible) : [...ahora];
    const b = new THREE.Box3(); ks.forEach(k => { const ms = MM[k]; if (ms && ms.geometry) { ms.geometry.computeBoundingBox(); const bb = ms.geometry.boundingBox.clone().translate(ms.position); b.union(bb); } });
    if (!b.isEmpty()) { const s = b.getBoundingSphere(new THREE.Sphere()); vm.ir(vm.az, vm.el, Math.max(260, s.radius * 3.2), s.center); }
    $('pasoDe').textContent = `Paso ${paso + 1} de ${PASOS.length}`; $('pasoNum').textContent = String(paso + 1).padStart(2, '0');
    $('pasoT').textContent = P.t; $('pasoD').textContent = P.d; $('consejo').innerHTML = P.tip ? `<b>Consejo · </b>${P.tip}` : '';
    // piezas que necesitas (agrupadas por catalogo)
    const cuenta = {};
    P.p.forEach(p => { const id = p.startsWith('T:') ? (Object.keys(TOR).find(k => p.includes(TOR[k])) || p) : M.piezas[p] || M.refs[p] ? (p.includes(':') ? p.split(':')[0] : p) : p; const ks = resolver(p);
      if (!cuenta[id]) cuenta[id] = { n: 0, ks: [] }; cuenta[id].n += p.includes(':') ? 1 : (porCat[id] ? porCat[id].cantidad : 1); cuenta[id].ks.push(...ks); });
    $('necesitas').innerHTML = Object.entries(cuenta).map(([id, x]) => { const c = porCat[id] || { nombre: id === 'munon' ? 'Su muñón + calceta' : id };
      const nombre = c.nombre.replace(/^(Índice|Medio|Anular|Meñique) · /, '$1 · ');
      return `<div class="pz"><b>${x.n}×</b><img alt="" src="${miniatura(x.ks.slice(0, 6), id + ':' + x.ks.length)}"><div>${nombre}</div></div>`; }).join('') || '<div class="gris" style="grid-column:1/-1;font-size:14px">Todas las piezas impresas (abajo puedes descargarlas).</div>';
    $('puntos').innerHTML = PASOS.map((_, j) => `<i class="${j <= paso ? 'on' : ''}" title="Paso ${j + 1}"></i>`).join('');
    $('puntos').querySelectorAll('i').forEach((el, j) => el.onclick = () => irPaso(j));
    $('ant').disabled = paso === 0; $('sig').textContent = paso === PASOS.length - 1 ? 'Inicio ↺' : 'Siguiente →';
  }
  vm.cada = dt => { if (anim < 1) { anim = Math.min(1, anim + dt / 0.9); const k = 1 - sm(anim);
    Object.values(MM).forEach(ms => { if (ms.userData.nuevo) { const d = ms.userData.dir.lengthSq() > 1 ? ms.userData.dir : new THREE.Vector3(0, 0, 60); ms.position.copy(d).multiplyScalar(k); } }); } };
  $('ant').onclick = () => irPaso(paso - 1); $('sig').onclick = () => irPaso(paso === PASOS.length - 1 ? 0 : paso + 1);
  irPaso(0);

  // ---------------------------------------------------------------- hero (gira sola) y explorador
  const vh = Visor($('heroLienzo'), { fondo: 0xffffff, auto: 0.25, dist: 760, el: 0.32, az: -2.5, quieto: false });
  const ph = Protesis(vh.escena); ph.refs.munon.ms.visible = false; { const s = ph.caja(); vh.centro.copy(s.center); vh.dist = s.radius * 3.9; }
  const ve = Visor($('explorarLienzo'), { fondo: 0x000000, dist: 820, el: 0.42, az: -2.3, rueda: true });
  const pe = Protesis(ve.escena); { const s = pe.caja(); ve.centro.copy(s.center); ve.dist = s.radius * 2.9; }
  const enfocar = (az, el, k, soloMano) => { const s = pe.caja(soloMano, !soloMano); ve.ir(az ?? ve.az, el ?? ve.el, s.radius * (k || 2.9), s.center); };
  const E = pe.est; let animC = false, animE = false, animG = false, tC = 0, tE = 0, tG = 0;
  const G0 = S.giro.neutra_desde_modelo;
  const leer = () => { const g = +$('cMuneca').value; E.cierre = $('cCierre').value / 100; E.muneca = G0 + g; E.codo = +$('cCodo').value; E.expl = $('cExpl').value / 100;
    $('lMuneca').textContent = g === 0 ? 'neutra' : g > 0 ? `pronación ${g}°` : `supinación ${-g}°`; pe.aplicar(); };
  ['cCierre', 'cMuneca', 'cCodo', 'cExpl'].forEach(id => $(id).oninput = () => { animC = animE = animG = false; marcar(); leer(); });
  function marcar() { document.querySelectorAll('#chipsExp [data-t]').forEach(b => b.classList.toggle('on', { anim: animC, giro: animG, explA: animE, guante: E.guante, transp: E.transp, munon: pe.refs.munon.ms.visible, ref: pe.refs.brazo_derecho.ms.visible }[b.dataset.t])); }
  document.querySelectorAll('#chipsExp [data-t]').forEach(b => b.onclick = () => {
    const t = b.dataset.t;
    if (t === 'anim') { animC = !animC; animE = false; tC = 0; $('cExpl').value = 0; }
    if (t === 'giro') { animG = !animG; animE = false; tG = 0; $('cExpl').value = 0; if (animG) enfocar(Math.PI / 2 + 0.25, 0.25, 2.2, true); }
    if (t === 'explA') { animE = !animE; animC = false; tE = 0; E.expl = 1; pe.aplicar(); enfocar(-2.3, 0.5, 2.4, true); }
    if (t === 'guante') E.guante = !E.guante; if (t === 'transp') E.transp = !E.transp;
    if (t === 'munon') pe.refs.munon.ms.visible = !pe.refs.munon.ms.visible;
    if (t === 'ref') { const r = pe.refs.brazo_derecho.ms; r.visible = !r.visible; marcar(); leer(); enfocar(Math.PI, 1.5, 2.4); }
    marcar(); leer();
  });
  const VISTA = { dorso: [Math.PI, 1.5, 2.6], palma: [Math.PI, -1.5, 2.6], lado: [0, 0.05, 2.6], mano: [0.35, 0.35, 3.2, true] };
  const desde = phi => { const c = Math.cos(phi), s_ = Math.sin(phi); return c >= 0 ? [0, Math.max(-1.5, Math.min(1.5, phi))] : [Math.PI, Math.max(-1.5, Math.min(1.5, Math.atan2(s_, -c)))]; };
  const FI = { dorso: Math.PI / 2, palma: -Math.PI / 2, lado: 0 };
  document.querySelectorAll('#chipsExp [data-v]').forEach(b => b.onclick = () => { const v = b.dataset.v, [a, e, k, m] = VISTA[v];
    if (FI[v] == null) return enfocar(a, e, k, m); const [az, el] = desde(FI[v] - E.muneca * Math.PI / 180); enfocar(az, el, k, m); });
  $('cCodo').addEventListener('change', () => enfocar());
  ve.cada = dt => {
    if (animC) { tC += dt; $('cCierre').value = Math.round(50 * (1 - Math.cos(tC * 1.5))); leer(); }
    if (animG) { tG += dt; $('cMuneca').value = Math.round(80 * Math.sin(tG * 0.9)); leer(); }
    if (animE) { tE += dt; const u = (tE % 6) / 6, v = u < 0.35 ? u / 0.35 : u < 0.6 ? 1 : u < 0.95 ? 1 - (u - 0.6) / 0.35 : 0; $('cExpl').value = Math.round(sm(v) * 100); leer(); }
  };
  marcar();

  // ---------------------------------------------------------------- socket + antebrazo
  const vs = Visor($('socketLienzo'), { fondo: 0x000000, dist: 620, el: 0.25, az: -0.4, rueda: true });
  const gs = new THREE.Group(); vs.escena.add(gs); const dirM = V3(mov.d).normalize(), plano = new THREE.Plane(new THREE.Vector3(0, 0, -1), 0);
  const sk = { P01: null, P02: null, 'C07:electrodo': null }, tor = [], pa2 = geo('P02', M.piezas.P02).attributes.position.array;
  function centroEn(y) { let sx = 0, sz = 0, n = 0; for (let i = 0; i < pa2.length; i += 3) if (Math.abs(pa2[i + 1] - y) < 3) { sx += pa2[i]; sz += pa2[i + 2]; n++; } return new THREE.Vector3(sx / n, y, sz / n); }
  ['P01', 'P02', 'C07:electrodo'].forEach(k => { const o = M.piezas[k], ms = new THREE.Mesh(geo(k, o), new THREE.MeshStandardMaterial({ color: o.color, roughness: 0.55, side: THREE.DoubleSide, clippingPlanes: [] })); gs.add(ms); sk[k] = ms; });
  Object.keys(M.piezas).filter(k => k.includes('tornillo_union')).forEach(k => { const o = M.piezas[k], ms = new THREE.Mesh(geo(k, o), mat('#b9c1cc', { metalness: 0.7, roughness: 0.3 }));
    const c = geo(k, o).boundingSphere.center.clone(), rad = c.clone().sub(centroEn(c.y)); rad.y = 0; rad.normalize(); ms.userData.rad = rad; ms.userData.y = c.y; gs.add(ms); tor.push(ms); });
  const mun = new THREE.Mesh(geo('r:munon', M.refs.munon), new THREE.MeshStandardMaterial({ color: '#c97a64', roughness: 0.7, side: THREE.DoubleSide, clippingPlanes: [] })); gs.add(mun);
  const cs = new THREE.Box3().setFromObject(sk.P01).getCenter(new THREE.Vector3()), cu = centroEn(tor.length ? tor[0].userData.y : cs.y + 40); vs.centro.copy(cu).addScaledVector(dirM, -25); vs.dist = 470;
  let animS = false, tS = 0;
  function socketAplicar() {
    const s = $('sSep').value / 100, c = $('sCorte').value / 100;
    sk.P01.position.copy(dirM).multiplyScalar(-90 * s); mun.position.copy(sk.P01.position); sk['C07:electrodo'].position.copy(sk.P01.position);
    tor.forEach(t => t.position.copy(t.userData.rad).multiplyScalar(35 * Math.min(1, s * 2.2)));
    plano.constant = c > 0 ? (cu.x + 60) - c * 60 : 1e6; plano.normal.set(-1, 0, 0);
    [sk.P01, sk.P02, sk['C07:electrodo'], mun].forEach(m => { m.material.clippingPlanes = c > 0 ? [plano] : []; m.material.needsUpdate = true; });
  }
  ['sSep', 'sCorte'].forEach(id => $(id).oninput = () => { animS = false; socketAplicar(); });
  document.querySelectorAll('[data-s]').forEach(b => b.onclick = () => { if (b.dataset.s === 'munon') { mun.visible = !mun.visible; b.classList.toggle('on', mun.visible); } else { animS = !animS; tS = 0; b.classList.toggle('on', animS); } });
  vs.cada = dt => { if (animS) { tS += dt; $('sSep').value = Math.round(50 * (1 - Math.cos(tS * 1.1))); socketAplicar(); } };
  socketAplicar();

  // ---------------------------------------------------------------- diferencial en 2D (vista desde el dorso)
  (function () {
    const D2 = M.mov.dif2d, sv = $('fSv'), NOMS = ['indice', 'medio', 'anular', 'menique']; if (!sv) return;
    const X = p => 210 - p[1] * 3.0, Y = p => 232 - p[0] * 3.0;                    // t a lo ancho (índice a la izquierda), s hacia arriba
    let caso = 'libre', k = 0, an = false, t = 0, pr = 0;
    const ln = (a, b, w, c, extra) => `<line x1="${X(a)}" y1="${Y(a)}" x2="${X(b)}" y2="${Y(b)}" stroke="${c}" stroke-width="${w * 0.75}" stroke-linecap="round" ${extra || ''}/>`;
    const dot = (p, r, c, f) => `<circle cx="${X(p)}" cy="${Y(p)}" r="${r}" fill="${f || '#fff'}" stroke="${c}" stroke-width="2"/>`;
    function dib() {
      const pasos = D2.casos[caso], f = k * (pasos.length - 1), i = Math.min(pasos.length - 2, Math.floor(f)), u = f - i, A = pasos[i], B = pasos[i + 1];
      const mix = (a, b) => [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u], P = key => NOMS.reduce((o, n) => (o[n] = mix(A[key][n], B[key][n]), o), {});
      const L = P('L'), Bj = P('B'), ang = NOMS.map((n, j) => A.ang[j] + (B.ang[j] - A.ang[j]) * u), cr = A.carro + (B.carro - A.carro) * u, fin = pasos[pasos.length - 1].ang;
      let g = '';
      NOMS.forEach((n, j) => {
        const q = D2.nudillos[n], tp = D2.puntas[n], c = Math.cos(ang[j] * Math.PI / 180), e = [q[0] + (tp[0] - q[0]) * c, q[1] + (tp[1] - q[1]) * c];
        const toca = caso !== 'libre' && fin[j] < 46 && k > 0.97;
        g += ln(q, e, 15, toca ? '#0c0c0c' : '#cecece') + (toca ? `<text x="${X(e)}" y="${Y(e) - 14}" text-anchor="middle" class="ft">toca</text>` : '');
      });
      NOMS.forEach(n => { g += ln(L[n], Bj[n], 2.5, '#6d6d6d'); });
      const p1 = mix(A.P1, B.P1), p2 = mix(A.P2, B.P2), pm = mix(A.PM, B.PM);
      g += ln(Bj.indice, p1, 7, '#0c0c0c') + ln(p1, Bj.medio, 7, '#0c0c0c') + ln(Bj.anular, p2, 7, '#0c0c0c') + ln(p2, Bj.menique, 7, '#0c0c0c');
      g += ln(p1, pm, 9, '#0c0c0c') + ln(pm, p2, 9, '#0c0c0c');
      g += `<rect x="${X(pm) - 22}" y="${Y(pm) - 6}" width="44" height="40" fill="none" stroke="#0c0c0c" stroke-width="2"/>`;
      NOMS.forEach(n => { g += dot(Bj[n], 4.5, '#0c0c0c') + dot(L[n], 4, '#6d6d6d'); });
      g += dot(p1, 5, '#0c0c0c') + dot(p2, 5, '#0c0c0c') + dot(pm, 5, '#0c0c0c', '#0c0c0c');
      $('fSvG').innerHTML = g;
      $('fLect').textContent = `carro ${cr.toFixed(1)} mm · nudillos (índice / medio / anular / meñique) ` + ang.map(a => a.toFixed(0) + '°').join(' / ');
    }
    document.querySelectorAll('[data-caso]').forEach(b => b.onclick = () => {
      caso = b.dataset.caso; document.querySelectorAll('[data-caso]').forEach(x => x.classList.toggle('on', x === b));
      k = 0; t = 0; $('fC').value = 0; an = true; $('fAnim').textContent = '❚❚ Pausa'; pr = performance.now(); requestAnimationFrame(paso);
    });
    $('fC').oninput = e => { an = false; $('fAnim').textContent = '▶ Animar'; k = e.target.value / 100; dib(); };
    $('fAnim').onclick = () => { an = !an; $('fAnim').textContent = an ? '❚❚ Pausa' : '▶ Animar'; pr = performance.now(); if (an) requestAnimationFrame(paso); };
    function paso(now) { if (!an) return; t += (now - pr) / 1000; pr = now; k = 0.5 - 0.5 * Math.cos(t * 1.3); $('fC').value = Math.round(k * 100); dib(); requestAnimationFrame(paso); }
    dib();
  })();


  // ---------------------------------------------------------------- dedo en 2D (mecanismo)
  (function () {
    const s = 2.9, K = [254, 122], L1 = 55 * s, L2 = 24 * s, G = [-4 * s, -9 * s], Cl = [3 * s, 5 * s], El = [51 * s, -7 * s], Fl = [6 * s, 6 * s], Kl = [-10 * s, 9 * s], Sv = [-38 * s, 5 * s], rh = 11 * s;
    const R = (a, p) => [Math.cos(a) * p[0] - Math.sin(a) * p[1], Math.sin(a) * p[0] + Math.cos(a) * p[1]], ad = (p, q) => [p[0] + q[0], p[1] + q[1]], d = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1]), rad = x => x * Math.PI / 180;
    const w1 = (a, p) => R(a, p), w2 = (a, b, p) => R(a, ad([L1, 0], R(b, p))), w3 = (a, b, c, p) => R(a, ad([L1, 0], R(b, ad([L2, 0], R(c, p)))));
    const LGC = d(w2(0, 0, Cl), G), LEF = d(w3(0, 0, 0, Fl), El), LHK = d([Sv[0], Sv[1] + rh], Kl);
    const raiz = (f, x0, lo, hi) => { let px = x0 + lo, pv = f(px); for (let x = x0 + lo + 0.5; x <= x0 + hi; x += 0.5) { const v = f(x); if (Math.sign(v) !== Math.sign(pv)) { let a = px, b = x; for (let k = 0; k < 30; k++) { const m = (a + b) / 2; if (Math.sign(f(m)) === Math.sign(f(a))) a = m; else b = m; } return (a + b) / 2; } px = x; pv = v; } return x0; };
    const res = c => { let b = 0, cc = 0, ph = 90; for (let i = 1; i <= 30; i++) { const a = rad(47 * c * i / 30); b = raiz(x => d(w2(a, rad(x), Cl), G) - LGC, b, -3, 25); cc = raiz(x => d(w3(a, rad(b), rad(x), Fl), w1(a, El)) - LEF, cc, -3, 25); ph = raiz(x => d(ad(Sv, [rh * Math.cos(rad(x)), rh * Math.sin(rad(x))]), w1(a, Kl)) - LHK, ph, -25, 25); } return [47 * c, b, cc, ph]; };
    const W = p => ad(K, p), ln = (id, p, q) => { const l = $(id); l.setAttribute('x1', p[0]); l.setAttribute('y1', p[1]); l.setAttribute('x2', q[0]); l.setAttribute('y2', q[1]); };
    function dib(c) { const [a, b, cc, ph] = res(c), A = rad(a), B = rad(b), C = rad(cc);
      $('dB1').setAttribute('transform', `translate(${K[0]},${K[1]}) rotate(${a})`); $('dB2').setAttribute('transform', `translate(${L1},0) rotate(${b})`); $('dB3').setAttribute('transform', `translate(${L2},0) rotate(${cc})`);
      const g = W(G), cw = W(w2(A, B, Cl)), e = W(w1(A, El)), f = W(w3(A, B, C, Fl)), k = W(w1(A, Kl)), sw = W(Sv), h = ad(sw, [rh * Math.cos(rad(ph)), rh * Math.sin(rad(ph))]);
      ln('dL1', g, cw); ln('dL2', e, f); ln('dL3', h, k); ln('dCuerno', sw, h); $('dEje').setAttribute('cx', sw[0]); $('dEje').setAttribute('cy', sw[1]);
      $('dPts').innerHTML = [g, cw, e, f, h, k].map(p => `<circle cx="${p[0]}" cy="${p[1]}" r="3.4"/>`).join(''); $('dLect').textContent = `nudillo ${a.toFixed(0)}° · media ${b.toFixed(0)}° · punta ${cc.toFixed(0)}°`; }
    let an = false, t = 0, pr = 0;
    $('dC').oninput = e => { an = false; $('dAnim').textContent = '▶ Animar'; dib(e.target.value / 100); };
    $('dAnim').onclick = () => { an = !an; $('dAnim').textContent = an ? '❚❚ Pausa' : '▶ Animar'; pr = performance.now(); if (an) requestAnimationFrame(paso); };
    function paso(now) { if (!an) return; t += (now - pr) / 1000; pr = now; const c = 0.5 - 0.5 * Math.cos(t * 1.4); $('dC').value = Math.round(c * 100); dib(c); requestAnimationFrame(paso); }
    dib(0);
  })();

  // ---------------------------------------------------------------- barra (clara/oscura) y menu
  const barra = $('barra'), oscuras = [...document.querySelectorAll('.oscuro-cap')];
  addEventListener('scroll', () => { const y = 32; barra.classList.toggle('oscuro', oscuras.some(s => { const r = s.getBoundingClientRect(); return r.top <= y && r.bottom > y; })); }, { passive: true });
  $('menuBtn').onclick = () => $('nav').classList.toggle('abierto'); $('nav').querySelectorAll('a').forEach(a => a.onclick = () => $('nav').classList.remove('abierto'));
  $('cargando').style.opacity = 0; setTimeout(() => $('cargando').remove(), 600);
})();
