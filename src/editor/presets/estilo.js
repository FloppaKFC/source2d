// Plugin de estilo: acciones visuales para dar vida a los objetos (pulso, aparicion, parpadeo).
return {
  actions: {
    pulsar: {
      label: "Pulsar tamano (estilo)",
      params: [{ k: "cantidad", l: "Cantidad", t: "number", d: "0.12" }, { k: "velocidad", l: "Velocidad", t: "number", d: "6" }],
      run: function (api) { var i = api.inst; if (!i) return; var k = 1 + api.arg("cantidad") * Math.sin(api.time * api.arg("velocidad")); i.scaleX = k; i.scaleY = k; }
    },
    aparecer: {
      label: "Aparecer suavemente (opacidad)",
      params: [{ k: "velocidad", l: "Velocidad", t: "number", d: "2" }],
      run: function (api) { var i = api.inst; if (!i) return; i.opacity = Math.min(1, (i.opacity || 0) + api.dt * api.arg("velocidad")); }
    },
    parpadear: {
      label: "Parpadeo suave (opacidad)",
      params: [{ k: "velocidad", l: "Velocidad", t: "number", d: "8" }],
      run: function (api) { var i = api.inst; if (!i) return; i.opacity = 0.6 + 0.4 * Math.sin(api.time * api.arg("velocidad")); }
    }
  },
  conditions: {}
};
