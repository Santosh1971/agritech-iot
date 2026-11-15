// Replaces flutter_service_worker.js on the server. Browsers that installed
// an older build's offline cache fetch this file when they check for an
// update; it deletes those caches and unregisters itself. New builds never
// register a service worker (web/flutter_bootstrap.js), so it runs only once
// per old install.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.map((k) => caches.delete(k))))
      .then(() => self.registration.unregister())
  );
});
