// Extension oficial de Source 2D: Steam.
// Logros, estadisticas, guardado en la nube, overlay, presencia y datos del jugador. Funciona en el juego exportado
// con la biblioteca de Steamworks al lado (libsteam_api.so en Linux, steam_api64.dll en Windows, del SDK de Steamworks)
// y, para probar fuera de Steam, un archivo steam_appid.txt con el numero de tu juego. Sin eso, "Steam esta
// disponible" es falso y lo demas no hace nada: el juego funciona igual (por ejemplo en itch.io).
// En expresiones: SteamNombre(), SteamIdioma(), SteamEstadistica("nombre").
var S = function (op, a, b, c) { try { return typeof __native !== 'undefined' && __native.steam ? __native.steam(op, a, b, c) : null; } catch (e) { return null; } };
var nube = function (api) { return 's2d_' + (api.text('ranura') || 'partida') + '.json'; };
return {
  actions: {
    logro: {
      label: "Steam: desbloquear un logro",
      params: [{ k: "logro", l: "Nombre del logro (API)", t: "text", d: "ACH_GANAR" }],
      run: function (api) { S(1, api.text("logro")); }
    },
    quitarLogro: {
      label: "Steam: quitar un logro (para probar)",
      params: [{ k: "logro", l: "Nombre del logro (API)", t: "text", d: "ACH_GANAR" }],
      run: function (api) { S(3, api.text("logro")); }
    },
    progreso: {
      label: "Steam: mostrar el progreso de un logro",
      params: [{ k: "logro", l: "Nombre del logro (API)", t: "text", d: "ACH_100_MONEDAS" },
               { k: "actual", l: "Actual", t: "number", d: "50" }, { k: "max", l: "Total", t: "number", d: "100" }],
      run: function (api) { S(4, api.text("logro"), api.arg("actual"), api.arg("max")); }
    },
    estadistica: {
      label: "Steam: poner una estadística",
      params: [{ k: "nombre", l: "Estadística (API)", t: "text", d: "monedas" }, { k: "valor", l: "Valor", t: "number", d: "0" }],
      run: function (api) { S(6, api.text("nombre"), api.arg("valor")); }
    },
    sumarEstadistica: {
      label: "Steam: sumar a una estadística",
      params: [{ k: "nombre", l: "Estadística (API)", t: "text", d: "monedas" }, { k: "valor", l: "Sumar", t: "number", d: "1" }],
      run: function (api) { var n = api.text("nombre"); S(6, n, (+S(5, n) || 0) + api.arg("valor")); }
    },
    guardarNube: {
      label: "Steam: guardar la partida en la nube",
      params: [{ k: "ranura", l: "Ranura", t: "text", d: "partida1" }],
      run: function (api) { S(7, nube(api), api.snapshot(true)); }
    },
    cargarNube: {
      label: "Steam: cargar la partida de la nube",
      params: [{ k: "ranura", l: "Ranura", t: "text", d: "partida1" }],
      run: function (api) { var t = S(8, nube(api)); if (t) api.restore(t); }
    },
    borrarNube: {
      label: "Steam: borrar la partida de la nube",
      params: [{ k: "ranura", l: "Ranura", t: "text", d: "partida1" }],
      run: function (api) { S(10, nube(api)); }
    },
    overlay: {
      label: "Steam: abrir el overlay",
      params: [{ k: "pagina", l: "Página", t: "select", d: "achievements",
                 options: [["achievements", "Logros"], ["friends", "Amigos"], ["community", "Comunidad"], ["stats", "Estadísticas"],
                           ["players", "Jugadores recientes"], ["settings", "Ajustes"], ["officialgamegroup", "Grupo del juego"]] }],
      run: function (api) { S(13, api.text("pagina")); }
    },
    overlayWeb: {
      label: "Steam: abrir una página web en el overlay",
      params: [{ k: "url", l: "Dirección", t: "text", d: "https://store.steampowered.com" }],
      run: function (api) { S(14, api.text("url")); }
    },
    tienda: {
      label: "Steam: abrir la tienda de un juego (DLC, secuela...)",
      params: [{ k: "app", l: "Número de la app", t: "number", d: "480" }],
      run: function (api) { S(15, api.arg("app")); }
    },
    presencia: {
      label: "Steam: poner la presencia (lo que ven tus amigos)",
      params: [{ k: "clave", l: "Clave", t: "text", d: "status" }, { k: "valor", l: "Texto", t: "text", d: "En el nivel 1" }],
      run: function (api) { S(17, api.text("clave"), api.text("valor")); }
    },
    quitarPresencia: {
      label: "Steam: borrar la presencia",
      params: [],
      run: function () { S(17, "", ""); }
    }
  },
  conditions: {
    disponible: {
      label: "Steam: Steam está disponible",
      params: [],
      test: function () { return !!S(18) || !!S(0); }
    },
    tieneLogro: {
      label: "Steam: el jugador tiene el logro",
      params: [{ k: "logro", l: "Nombre del logro (API)", t: "text", d: "ACH_GANAR" }],
      test: function (api) { return !!S(2, api.text("logro")); }
    },
    hayNube: {
      label: "Steam: hay guardado en la nube",
      params: [{ k: "ranura", l: "Ranura", t: "text", d: "partida1" }],
      test: function (api) { return !!S(9, nube(api)); }
    },
    nubeActiva: {
      label: "Steam: la nube está activada",
      params: [],
      test: function () { return !!S(11); }
    },
    overlayActivo: {
      label: "Steam: el overlay está disponible",
      params: [],
      test: function () { return !!S(16); }
    },
    estadisticaCmp: {
      label: "Steam: comparar una estadística",
      params: [{ k: "nombre", l: "Estadística (API)", t: "text", d: "monedas" },
               { k: "op", l: "", t: "select", d: "ge", options: [["ge", "≥"], ["gt", ">"], ["eq", "="], ["le", "≤"], ["lt", "<"]] },
               { k: "valor", l: "Valor", t: "number", d: "100" }],
      test: function (api) {
        var v = +S(5, api.text("nombre")) || 0, w = api.arg("valor"), o = api.text("op");
        return o === "gt" ? v > w : o === "eq" ? v === w : o === "le" ? v <= w : o === "lt" ? v < w : v >= w;
      }
    }
  }
};
