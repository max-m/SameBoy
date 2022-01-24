const cache_name = VERSION;
const EXTRA_FILES = [
	'./',
	'https://fonts.googleapis.com/css2?family=Open+Sans+Condensed:wght@300&family=Raleway:wght@300&display=swap',
];

self.addEventListener('install', event => {
	console.log('[Service Worker] Install');

	event.waitUntil((async () => {
		const cache = await caches.open(cache_name);

		console.log('[Service Worker] Caching files');

		await cache.addAll(EXTRA_FILES.concat(FILES));
	})());
});

self.addEventListener('fetch', event => {
	event.respondWith((async () => {
		const request = await caches.match(event.request);

		console.log(`[Service Worker] Fetching resource: ${event.request.url}`);

		if (request) {
			return request;
		}

		const response = await fetch(event.request);
		const cache = await caches.open(cache_name);

		console.log(`[Service Worker] Caching new resource: ${event.request.url}`);
		cache.put(event.request, response.clone());

		return response;
	})());
});

self.addEventListener('activate', event => {
	event.waitUntil(caches.keys().then(keys => {
		return Promise.all(keys.map(key => {
			if (key === cache_name) {
				return;
			}

			console.log(`[Service Worker] Clearing cache key: ${key}`);
			return caches.delete(key);
		}));
	}));
});
