(function () {
  // The visitor's system: show the download for it first, the others one click away.
  var s = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "") + " " + (navigator.userAgent || "");
  s = s.toLowerCase();
  var os = null;
  if (!/iphone|ipad|android/.test(s)) os = /mac/.test(s) ? "mac" : /win/.test(s) ? "win" : /linux|x11|cros/.test(s) ? "linux" : null;
  var names = { mac: "macOS", win: "Windows", linux: "Linux" };
  function apply(o) {
    if (o) document.documentElement.setAttribute("data-os", o);
    document.querySelectorAll("[data-os-name]").forEach(function (el) { el.textContent = o ? names[o] : ""; });
    document.querySelectorAll("[data-os-hide]").forEach(function (el) { el.hidden = !o; });
  }
  apply(os);
  document.querySelectorAll("[data-pick]").forEach(function (a) {
    a.addEventListener("click", function (e) { e.preventDefault(); apply(a.getAttribute("data-pick")); });
  });

  // Screenshots open full size in a lightbox.
  var box = document.createElement("div");
  box.className = "lightbox";
  var big = document.createElement("img");
  box.appendChild(big);
  document.body.appendChild(box);
  function close() { box.classList.remove("active"); }
  box.addEventListener("click", close);
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });
  document.querySelectorAll("img[data-zoom]").forEach(function (img) {
    img.addEventListener("click", function () { big.src = img.currentSrc || img.src; big.alt = img.alt; box.classList.add("active"); });
  });
})();
