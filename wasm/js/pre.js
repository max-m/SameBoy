if (typeof Module.SAMEBOY_DEBUG === 'undefined') {
	Module.SAMEBOY_DEBUG = false;
}

const statusElement = document.getElementById('status');
const progressElement = document.getElementById('progress');
const spinnerElement = document.getElementById('spinner');

if (Module.SAMEBOY_DEBUG) {
	window.SameBoy = Module;
}

Module.logReadFiles = true;

Module.printWithColors = true;

Module.print = function() {
	console.log.apply(console, arguments);
};

Module.printErr = function() {
	console.error.apply(console, arguments);
};

Module.canvas = (() => {
	const canvas = document.getElementById('canvas');

	// As a default initial behavior, pop up an alert when webgl context is lost. To make your
	// application robust, you may want to override this behavior before shipping!
	// See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
	canvas.addEventListener("webglcontextlost", function(e) {
		e.preventDefault();
		alert('WebGL context lost. You will need to reload the page.');
	}, false);

	canvas.addEventListener('contextmenu', event => event.preventDefault())

	canvas.setAttribute('tabindex', '-1');

	return canvas;
})();

Module.setStatus = text => {
	console.debug(`[Status] ${text}`);

	if (!Module.setStatus.last) {
		Module.setStatus.last = {
			time: Date.now(),
			text: ''
		};
	}

	text = text ? text.trim() : '';

	if (text === Module.setStatus.last.text) {
		return;
	}

	// Message (done / total)
	const progress = text.match(/([^(]+)\((\d+(\.\d+)?)\s*\/\s*(\d+)\)/);

	const now = Date.now();

	// if this is a progress update, skip it if too soon
	if (progress && now - Module.setStatus.last.time < 30) {
		return;
	}

	Module.setStatus.last.time = now;
	Module.setStatus.last.text = text;

	if (progress) {
		text = progress[1];
		progressElement.value = parseInt(progress[2]) * 100;
		progressElement.max = parseInt(progress[4]) * 100;
		progressElement.hidden = false;
		spinnerElement.hidden = false;
	}
	else {
		progressElement.value = null;
		progressElement.max = null;
		progressElement.hidden = true;
		if (!text) spinnerElement.hidden = true;
	}

	statusElement.innerHTML = text;
};

Module.gb_touch_keymap = {
	up:     { keyCode: 38 }, // ArrowUp
	down:   { keyCode: 40 }, // ArrowDown
	left:   { keyCode: 37 }, // ArrowLeft
	right:  { keyCode: 39 }, // ArrowRight
	a:      { keyCode: 88 }, // x
	b:      { keyCode: 90 }, // z
	start:  { keyCode: 13 }, // Enter
	select: { keyCode: 8 },  // Backspace
	menu:   { keyCode: 27 }, // Escape
};

Module.gb_load_rom_buffer = function (name, data) {
	document.body.dispatchEvent(new Event('click'));

	const pos = name.lastIndexOf('.');
	const battery_name = name.substr(0, pos < 0 ? name.length : pos) + '.sav';
	const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), ALLOC_NORMAL);

	// Copy data into WASM memory
	const ptr = Module._malloc(data.byteLength);
	const wasm_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, data.byteLength);
	wasm_buf.set(data);

	Module._load_rom(wasm_buf.byteOffset, wasm_buf.byteLength, battery_path);
};

Module.gb_load_remote_rom = async function (url) {
	const request = new Request(url);

	const name = (_ => {
		const name = url.substring(url.lastIndexOf('/') + 1);

		if (name.endsWith('.gb') || name.endsWith('.gbc')) {
			return name
		}
		else if (name.length) {
			return `${name}.gb`
		}

		return string_hash(url)
	})()

	Module._pause();
	Module.setStatus(`Fetching ${name}`);

	const response = await fetch(request);
	const content_length = Number.parseInt(response.headers.get('Content-Length')) || 0;

	if (!response.ok) {
		throw new Error('HTTP error, status = ' + response.status);
	}

	let buf;

	if (content_length) {
		Module.setStatus(`Fetching ${name} (0 / ${content_length})`);

		buf = new Uint8Array(content_length);
		const reader = response.body.getReader();
		let received_length = 0;

		while (true) {
			const { done, value } = await reader.read();

			if (done) {
				break;
			}

			if (received_length + value.length <= buf.length) {
				buf.set(value, received_length);
			}
			else {
				console.warn(`Content-Length header underreported the length:\nContent-Length: ${content_length}\nReceived: ${received_length + value.length}`);

				// Copy data to a new buffer :(
				const tmp = new Uint8Array(received_length + value.length);
				tmp.set(buf);
				tmp.set(value, received_length);
				buf = tmp;
			}

			received_length += value.length;

			Module.setStatus(`Fetching ${name} (${received_length} / ${content_length})`);
		}
	}
	else {
		buf = new Uint8Array(await response.arrayBuffer());
	}

	Module.setStatus();
	Module._resume();

	Module.gb_load_rom_buffer(name, buf);
};

Module.gb_open_file = function (event) {
	const file = event instanceof File
	           ? event
	           : (event.dataTransfer || event.target).files[0];

	return new Promise((resolve, reject) => {
		const reader = new FileReader();
		const name = file.name;

		reader.onload = () => {
			const data = new Uint8Array(reader.result);

			const pos = name.lastIndexOf('.');
			const battery_name = name.substr(0, pos < 0 ? name.length : pos) + '.sav';
			const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), ALLOC_NORMAL);

			// Copy data into WASM memory
			const ptr = Module._malloc(data.byteLength);
			const wasm_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, data.byteLength);
			wasm_buf.set(new Uint8Array(data));

			Module._load_rom(wasm_buf.byteOffset, wasm_buf.byteLength, battery_path);
			resolve();
		}
		reader.onabort = reject;
		reader.onerror = reject;

		reader.readAsArrayBuffer(file);
	});
}

Module.gb_syncfs_in_progress = false;
Module.gb_syncfs_needs_sync = false;
Module.gb_syncfs = async function (populate = false) {
	if (Module.gb_syncfs_in_progress) {
		Module.gb_syncfs_needs_sync = { populate };
		return;
	}
	Module.gb_syncfs_in_progress = true;
	console.log("Syncing file system ...");

	return await new Promise((resolve, reject) => {
		FS.syncfs(populate, function (err) {
			Module.gb_syncfs_in_progress = false;

			if (err) {
				reject(err);
			}
			else if (Module.gb_syncfs_needs_sync) {
				console.log("A sync was requested while syncing, syncing again.");
				const populate = Module.gb_syncfs_needs_sync.populate;
				Module.gb_syncfs_needs_sync = undefined;

				Module.gb_syncfs(populate)
					.then(resolve)
					.catch(reject);

				return;
			}
			else {
				console.log("File system synchronized.");

				resolve()
			}
		});

	});
};

Module.gb_list_save_files = () => {
	return FS.readdir('/persist')
		.filter(entry => entry.endsWith('.sav'))
}

Module.gb_open_save_manager = () => {
	Module._pause();

	const elem = document.getElementById('saveManager');
	elem.style.display = '';
	elem.innerHTML = '';

	const table = document.createElement('table');
	const tbody = document.createElement('tbody');
	table.appendChild(tbody);

	const tr = document.createElement('tr');
	const td = document.createElement('td');
	td.setAttribute('colspan', '3');
	const span = document.createElement('span');
	span.innerText = 'CLOSE';
	span.classList.add('closeButton');
	span.addEventListener('click', Module.gb_close_save_manager);
	td.appendChild(span);
	tr.appendChild(td);
	tbody.appendChild(tr);

	for (let save of Module.gb_list_save_files()) {
		const tr = document.createElement('tr');

		const td1 = document.createElement('td');
		td1.innerHTML = '<svg class="download"><use href="img/download.svg#i"></use></svg>'
		td1.firstChild.addEventListener('click', () => {
			const data = FS.readFile(`/persist/${save}`, { encoding: 'binary' });

			const blob = new Blob([data], {
				type: 'application/octet-stream'
			});

			const url = window.URL.createObjectURL(blob);

			setTimeout(() => window.URL.revokeObjectURL(url), 1000);

			const anchor = document.createElement('a');
			anchor.href = url;
			anchor.download = save;
			anchor.style.display = 'none';
			document.body.appendChild(anchor);
			anchor.click();
			anchor.remove();
		});
		tr.append(td1);

		const td2 = document.createElement('td');
		td2.innerHTML = '<svg class="delete"><use href="img/delete.svg#i"></use></svg>'
		td2.firstChild.addEventListener('click', async () => {
			if (window.confirm(`Are you sure you want to delete “${save}”?`)) {
				FS.unlink(`/persist/${save}`);

				await Module.gb_syncfs();

				if (!FS.analyzePath(`/persist/${save}`).exists) {
					tr.remove();
				}
			}
		});
		tr.append(td2);

		const td3 = document.createElement('td');
		td3.innerText = save;
		tr.append(td3);

		tbody.appendChild(tr);
	}

	elem.appendChild(table);
}

Module.gb_close_save_manager = () => {
	const elem = document.getElementById('saveManager');
	elem.style.display = 'none';
	elem.innerHTML = '';

	Module._resume();
}

Module.gb_set_system_color = (r, g, b) => {
	const system = document.getElementById('system');
	system.classList.add('forceLight');
	system.style.setProperty('--system-color', `rgb(${r}, ${g}, ${b})`);
}

Module.gb_camera_remove = () => {
	if (Module.GbCamera && Module.GbCamera.remove) {
		Module.GbCamera.remove();
	}
}

Module.GbCamera = 'unloaded';
Module.gb_camera_init = () => {
	if (Module.GbCamera === undefined) {
		// Error
		return -1;
	}

	if (Module.GbCamera === 'unloaded') {
		Module.GbCamera = { };

		Module._pause();
		Module.setStatus('Loading Camera module (0 / 1)');

		import('./js/camera.js')
			.then(camera => Module.GbCamera = camera.default(Module))
			.catch(error => {
				console.error(error);

				Module.GbCamera = undefined;
				delete Module.GbCamera;
			})
			.finally(() => {
				Module.setStatus();
				Module._resume();
			});
	}

	if (Module.GbCamera.init) {
		return Module.GbCamera.init();
	}

	// Not yet ready
	return 1;
}

Module.gb_rumble = (index, amp, duration) => {
	// Check if a gamepad is in use
	if (index >= 0) {
		const pads = navigator.getGamepads();

		if (pads[index]) {
			// The gamepad rumble interface is still experimental.
			if (pads[index].hapticActuators && pads[index].hapticActuators[0]) { // Firefox
				pads[index].hapticActuators[0].pulse(amp, duration);
				return;
			}
			else if (pads[index].vibrationActuator && pads[index].vibrationActuator.playEffect) { // Chrome
				pads[index].vibrationActuator.playEffect('dual-rumble', {
					duration: duration,
					startDelay: 0,
					strongMagnitude: amp,
					weakMagnitude: amp
				});
				return;
			}
		}
	}

	// Try to use the Vibration API as fallback.
	// Calls to this function get silently ignored in “Do not disturb“ mode for example.
	if (navigator.vibrate) {
		if (amp == 1.0) return navigator.vibrate(duration);
		if (amp == 0.0) return navigator.vibrate(0);

		const steps = 10;
		const on  = (duration / (steps / 2)) * amp;
		const off = (duration / (steps / 2)) * (1.0 - amp);

		const pattern = [ on ];
		let remaining = duration - on;
		let is_off = true;

		while (remaining > 0) {
			if (is_off) {
				remaining -= off;
				pattern.push(off);
			}
			else {
				remaining -= on;
				pattern.push(on);
			}
			is_off = !is_off;
		}

		navigator.vibrate(pattern);
	}
}

Module.setStatus('Downloading...');

window.onerror = function() {
	Module.setStatus('Exception thrown, see JavaScript console');
	spinnerElement.style.display = 'none';
	Module.setStatus = function(text) {
		if (text) Module.printErr('[post-exception status] ' + text);
	};
};

Module.ready.then(async () => {
	FS.mkdir('/persist');
	FS.mount(IDBFS, { }, '/persist');

	await Module.gb_syncfs(true);

	// Call the exported init function
	Module._init();
});
