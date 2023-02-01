const VERSION = "0.15.8_b07e3e4";
const FILES = [
	"index.html",
	"index.js",
	"index.data",
	"index.wasm",
	"sameboy.webmanifest",
	"js/camera.js",
	"js/motion-sensors.js",
	"js/sensor.js",
	"js/simple-keyboard.min.js",
	"js/zip-no-worker-deflate.min.js",
	"img/icon-16x16.png",
	"img/icon-32x32.png",
	"img/icon-64x64.png",
	"img/icon-128x128.png",
	"img/icon-256x256.png",
	"img/icon-512x512.png",
	"css/sgb.css",
	"css/simple-keyboard.css",
	"css/main.css",
	"css/mobile-portrait.css",
	"css/agb.css",
	"css/base.css",
	"css/cgb.css",
	"css/mobile-landscape.css",
	"css/dmg.css",
	"css/mgb.css",
	"img/torch.svg",
	"img/apple-touch-icon-precomposed.png",
	"img/icon-maskable-192x192.png",
	"img/front_text.svg",
	"img/workboy.svg",
	"img/workboy/conversion.svg",
	"img/workboy/clock.svg",
	"img/workboy/world.svg",
	"img/workboy/date.svg",
	"img/workboy/temps.svg",
	"img/workboy/phone.svg",
	"img/workboy/money.svg",
	"img/workboy/records.svg",
	"img/workboy/calc.svg",
	"img/noise.png",
	"img/delete.svg",
	"img/favicon.ico",
	"img/icon-maskable-512x512.png",
	"img/fullscreen.svg",
	"img/icon-192x192.png",
	"img/safari-pinned-tab.svg",
	"img/flip.svg",
	"img/apple-touch-icon.png",
	"img/og-image.png",
	"img/download.svg",

];
const cache_name = VERSION;
const EXTRA_FILES = [ './' ];

self.addEventListener('install', event => {
	console.log('[Service Worker] Install');

	self.skipWaiting();

	event.waitUntil((async () => {
		try {
			const cache = await caches.open(cache_name);
			const files = EXTRA_FILES.concat(FILES);
			const total = files.length;
			let cached = 0;

			console.log(`[Service Worker] Caching ${total} files`);

			await Promise.all(files.map(async (url) => {
				let controller;

				try {
					controller = new AbortController();
					const signal = controller.signal;

					const req = new Request(url, { cache: 'reload' });
					const res = await fetch(req, { signal });

					if (res && res.status === 200) {
						await cache.put(req, res.clone());
						cached += 1;
					}
					else {
						console.error(`[Service Worker] Failed to fetch ${url}:`, res);
					}
				}
				catch (e) {
					console.error(`[Service Worker] Failed to fetch ${url}:`, e);
					controller.abort();
				}
			}));

			if (cached === total) {
				console.log('[Service Worker] All files cached successfully.');
			}
			else {
				console.warn(`[Service Worker] Failed to cache ${total - cached} of ${total} files.`)
			}
		}
		catch (e) {
			console.error('[Service Worker] Failed to install:', e)
		}
	})());
});

self.addEventListener('fetch', event => {
	event.respondWith((async () => {
		const cachedResponse = await caches.match(event.request);

		console.log(`[Service Worker] Fetching resource: ${event.request.url}`);

		if (cachedResponse) {
			// TODO: Should we also check the Cache-Control header?
			const expires = cachedResponse.headers.get('Expires');

			// `new Date(null)` returns the current date
			if (expires) {
				const date = new Date(expires);

				// Date.getTime() returns NaN for invalid dates
				if (date.getTime() === date.getTime() && new Date() <= date) {
					return cachedResponse;
				}
			}
			else {
				return cachedResponse;
			}
		}

		const response = await fetch(event.request);
		const cache = await caches.open(cache_name);

		console.log(`[Service Worker] Caching new resource: ${event.request.url}`);
		await cache.put(event.request, response.clone());

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
