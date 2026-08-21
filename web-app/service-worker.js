// Bump this whenever a cache-first file below changes (js/*, vendor/*,
// icons/*, favicon.ico, tank-hero.svg) -- installed clients only refetch
// them once the browser detects service-worker.js itself has new bytes.
const CACHE_NAME = 'betacrawler-web-app-v2';
const SHELL_FILES = [
  './', './index.html', './manifest.json',
  './js/app.js', './js/api.js', './js/webserial-link.js', './js/device-model.js',
  './js/terminal.js', './js/settings-ini.js', './js/protocol.js', './js/line-buffer.js',
  './vendor/bootstrap.min.css', './vendor/bootstrap.bundle.min.js', './vendor/alpine.min.js',
  './pages/home.html', './pages/config.html', './pages/controller.html', './pages/modes.html',
  './pages/terminal.html', './pages/help.html', './pages/wiring.html',
  './favicon.ico', './tank-hero.svg',
  './icons/icon-192.png', './icons/icon-512.png',
];

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE_NAME).then((cache) => cache.addAll(SHELL_FILES)));
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((names) =>
      Promise.all(names.filter((n) => n !== CACHE_NAME).map((n) => caches.delete(n)))
    )
  );
  self.clients.claim();
});

// Cache-first for the immutable shell, network-first for navigations and page
// fragments. Those change on every edit, and a cache-first worker registered
// on localhost -- the same origin Tasks 8-10 iterate on -- otherwise serves
// yesterday's markup for the rest of the session.
self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  const liveFirst = event.request.mode === 'navigate' || url.pathname.includes('/pages/');
  if (liveFirst) {
    event.respondWith(
      fetch(event.request)
        .then((resp) => {
          const copy = resp.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, copy));
          return resp;
        })
        .catch(() => caches.match(event.request)
          .then((cached) => cached || caches.match('./index.html')))
    );
    return;
  }
  event.respondWith(caches.match(event.request).then((cached) => cached || fetch(event.request)));
});
