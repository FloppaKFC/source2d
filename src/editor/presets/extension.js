// Extension: devuelve las acciones y condiciones nuevas.
// api.text(k) = texto del parametro k; api.arg(k) = numero o expresion (Jugador.X + 10)
// api.inst = instancia del evento; api.vars = variables globales; api.log(msg) = consola
return {
  actions: {
    saludar: {
      label: "Saludar en consola",
      params: [{ k: "texto", l: "Texto", t: "text", d: "Hola" }],
      run: function (api, targets) { api.log(api.text("texto")); }
    }
  },
  conditions: {
    mitad: {
      label: "Coordenada X mayor que el parametro",
      params: [{ k: "valor", l: "Valor", t: "number", d: "100" }],
      test: function (api) { return !!(api.inst && api.inst.x > api.arg("valor")); }
    }
  }
};
