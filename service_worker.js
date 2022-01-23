const VERSION = "0.14.7_85d0810";
const FILES = [
	"index.html",
	"index.js",
	"index.data",
	"index.wasm",
	"sameboy.webmanifest",
	"js/camera.js",
	"js/motion-sensors.js",
	"js/sensor.js",
	"img/icon-16x16.png",
	"img/icon-32x32.png",
	"img/icon-64x64.png",
	"img/icon-128x128.png",
	"img/icon-256x256.png",
	"img/icon-512x512.png",
	"img/download.svg",
	"img/delete.svg",
	"img/fullscreen.svg",
	"img/favicon.ico",
	"img/icon-maskable-512x512.png",
	"img/icon-maskable-192x192.png",
	"img/icon-192x192.png",
	"img/apple-touch-icon-precomposed.png",
	"img/noise.png",
	"img/safari-pinned-tab.svg",
	"img/flip.svg",
	"img/apple-touch-icon.png",
	"img/torch.svg",
	"css/mobile-portrait.css",
	"css/agb.css",
	"css/base.css",
	"css/cgb.css",
	"css/mgb.css",
	"css/main.css",
	"css/sgb.css",
	"css/mobile-landscape.css",
	"css/dmg.css",

];
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
