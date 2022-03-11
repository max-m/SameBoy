if (typeof Module.SAMEBOY_DEBUG === 'undefined') {
	Module.SAMEBOY_DEBUG = false;
}

if ('serviceWorker' in navigator) {
	navigator.serviceWorker
		.register('./service_worker.js')
		.then(reg => console.log('Registered ServiceWorker:', reg))
		.catch(err => console.error('Failed to register ServiceWorker:', err));
};

const statusElement = document.getElementById('status');
const progressElement = document.getElementById('progress');
const spinnerElement = document.getElementById('spinner');

if (Module.SAMEBOY_DEBUG) {
	window.SameBoy = Module;
}

function on_escape(fn) {
	return function (event) {
		if (event.key === 'Escape' || event.key === 'Esc' || event.keyCode === 27) {
			event.preventDefault();
			fn(event)
		}
	}
}

Module.logReadFiles = true;

Module.printWithColors = false;

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
	canvas.addEventListener('webglcontextlost', event => {
		event.preventDefault();
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
	})();

	Module._pause();
	Module.setStatus(`Fetching ${name}`);

	let buf = null;

	try {
		const response = await fetch(request);
		const content_length = Number.parseInt(response.headers.get('Content-Length')) || 0;

		if (!response.ok) {
			throw new Error('HTTP error, status = ' + response.status);
		}


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
	}
	catch (err) {
		console.error(err);
		// TODO: Show error to user
	}

	Module.setStatus(null);
	Module._resume();

	if (buf) {
		Module.gb_load_rom_buffer(name, buf);
	}
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
	console.log('Syncing file system ...');

	return await new Promise((resolve, reject) => {
		FS.syncfs(populate, function (err) {
			Module.gb_syncfs_in_progress = false;

			if (err) {
				reject(err);
			}
			else if (Module.gb_syncfs_needs_sync) {
				console.log('A sync was requested while syncing, syncing again.');
				const populate = Module.gb_syncfs_needs_sync.populate;
				Module.gb_syncfs_needs_sync = undefined;

				Module.gb_syncfs(populate)
					.then(resolve)
					.catch(reject);

				return;
			}
			else {
				console.log('File system synchronized.');

				resolve()
			}
		});

	});
};

Module.gb_list_save_files = () => {
	return FS.readdir('/persist')
		.filter(entry => entry.endsWith('.sav'))
}

Module.gb_close_save_manager = () => {
	document.removeEventListener('keyup', Module.gb_close_save_manager_on_keyup);

	const manager = document.getElementById('saveManager');
	manager.style.display = 'none';

	const elem = manager.querySelector('.dialogContent');
	elem.innerHTML = '';

	Module._resume();
}
Module.gb_close_save_manager_on_keyup = on_escape(Module.gb_close_save_manager);
Module.gb_open_save_manager = () => {
	Module._pause();

	document.addEventListener('keyup', Module.gb_close_save_manager_on_keyup);

	const manager = document.getElementById('saveManager');
	manager.style.display = '';

	const close = manager.querySelector('.closeButton');
	close.addEventListener('click', Module.gb_close_save_manager);

	const elem = manager.querySelector('.dialogContent');
	elem.innerHTML = '';

	const table = document.createElement('table');
	const tbody = document.createElement('tbody');
	table.appendChild(tbody);

	for (let save of Module.gb_list_save_files()) {
		const tr = document.createElement('tr');

		const td1 = document.createElement('td');
		td1.innerHTML = '<svg class="icon download"><use href="img/download.svg#i"></use></svg>'
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
		td2.innerHTML = '<svg class="icon delete"><use href="img/delete.svg#i"></use></svg>'
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

Module.gb_set_system_color = (r, g, b) => {
	const system = document.getElementById('system');
	system.classList.add('forceLight');
	system.style.setProperty('--system-color', `rgb(${r}, ${g}, ${b})`);
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
				Module.setStatus(null);
				Module._resume();
			});
	}

	if (Module.GbCamera.init) {
		return Module.GbCamera.init();
	}

	// Not yet ready
	return 1;
}

Module.gb_camera_remove = () => {
	if (Module.GbCamera && Module.GbCamera.remove) {
		Module.GbCamera.remove();
	}
}

Module.GbAccelerometer = undefined;
Module.gb_accelerometer_stop = () => {
	if (Module.GbAccelerometer) {
		Module.GbAccelerometer.stop();
		Module.GbAccelerometer = undefined;
	}
}
Module.gb_accelerometer_init = () => {
	if (Module.GbAccelerometer) {
		return;
	}

	Module._pause();
	Module.setStatus('Loading Accelerometer module (0 / 1)');
	import('./js/motion-sensors.js')
		.then(({ Accelerometer }) => {
			function start() {
				// Kirby - Tilt ’n’ Tumble uses an ADXL202 2-axis accelerometer.
				// Its R_SET resistor seems to be 120 kΩ,
				// which would mean that its accelerometer samples 1000 times per second.
				// I think 60 updates should be enough for us.
				const sensor = new Accelerometer({ frequency: 60 });

				sensor.addEventListener('reading', event => {
					// https://www.w3.org/TR/accelerometer/#model
					// The acceleration is the rate of change of velocity of a device with respect to time.
					// Its unit is the metre per second squared (m/s2)
					Module._set_accelerometer_values(
						sensor.x / 9.80665,
						-sensor.y / 9.80665 // The Y axis of the sensor points down
					);
				});

				sensor.start();

				Module.GbAccelerometer = sensor;
			}

			if (navigator.permissions) {
				navigator.permissions.query({ name: 'accelerometer' })
				.then(result => {
					if (result.state === 'granted') {
						start();
					}
					else {
						console.error('Accelerometer permission has been denied.');
					}
				})
				.catch(err => {
					console.error('Permission query failed, trying anyway:', err);
					start();
				});
			}
		})
		.catch(error => {
			console.error(error);
		})
		.finally(() => {
			Module.setStatus(null);
			Module._resume();
		});
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
		if (amp >= 1.0) return navigator.vibrate(duration);
		if (amp <= 0.0) return navigator.vibrate(0);

		// Create a PWM pattern (amp% of duration = on at 100%)
		const steps = 10; // number of on / off steps in the PWM patern
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

Module.gb_close_about_dialog = () => {
	document.removeEventListener('keyup', Module.gb_close_about_on_keyup);
	document.getElementById('about').style.display = 'none';

	Module._resume();
}
Module.gb_close_about_on_keyup = on_escape(Module.gb_close_about_dialog);
Module.gb_open_about_dialog = () => {
	Module._pause();

	document.addEventListener('keyup', Module.gb_close_about_on_keyup);

	const about = document.getElementById('about');
	about.style.display = '';

	const close = about.querySelector('.closeButton');
	close.addEventListener('click', Module.gb_close_about_dialog);
}

Module.gb_close_printer_dialog = () => {
	document.removeEventListener('keyup', Module.gb_close_printer_on_keyup);

	const dialog = document.getElementById('printerDialog');
	dialog.style.display = 'none';

	Module._resume();
}
Module.gb_close_printer_on_keyup = on_escape(Module.gb_close_printer_on_keyup);
Module.gb_printer_delete_all = () => {
	const parent = document.querySelector('#printerDialog .prints');

	if (parent.querySelectorAll('canvas').length == 0) {
		return;
	}

	if (window.confirm(`Are you sure you want to delete all prints?`)) {
		while (parent.firstChild) {
			parent.firstChild.remove()
		}
		delete parent.dataset.game;
	}
}
Module.gb_printer_download_all = async () => {
	const canvases = [...document.querySelectorAll('#printerDialog .prints canvas')];

	if (canvases.length == 0) {
		return;
	}
	else if (canvases.length == 1) {
		canvases[0]
			.parentElement
			.querySelector('.download')
			.dispatchEvent(new Event('click'));
	}

	Module.setStatus('Loading Zip module (0 / 1)');
	const zip = await import('./js/zip-no-worker-deflate.min.js');
	zip.configure({
		useWebWorkers: false
	});

	const blob_writer = new zip.BlobWriter('application/zip');
	const writer = new zip.ZipWriter(blob_writer);

	const game_name = document.querySelector('#printerDialog .prints').dataset.game;

	let i = 0;
	const total = canvases.length;
	Module.setStatus(`Zipping ... (0/${total})`);

	await Promise.all(canvases.map(async canvas => {
		const blob = await new Promise((resolve, reject) => {
			canvas.toBlob(blob => {
				if (blob == null) {
					reject();
				}
				else {
					resolve(blob);
				}
			}, 'image/png');
		});

		const date_str = canvas.parentElement.dataset.date_str;
		const filename = `${date_str}.png`;
		const reader = new zip.BlobReader(blob);

		return writer.add(filename, reader).then(() => {
			Module.setStatus(`Zipping ... (${++i}/${total})`);
		});
	}));

	await writer.close();
	Module.setStatus(null);

	const blob = blob_writer.getData();
	const url = window.URL.createObjectURL(blob);
	setTimeout(() => window.URL.revokeObjectURL(url), 1000);

	const anchor = document.createElement('a');
	anchor.href = url;
	anchor.download = `${game_name}.zip`;
	anchor.style.display = 'none';
	document.body.appendChild(anchor);
	anchor.click();
	anchor.remove();
}
Module.gb_open_printer_dialog = () => {
	Module._pause();

	document.addEventListener('keyup', Module.gb_close_printer_on_keyup);

	const dialog = document.getElementById('printerDialog');
	dialog.style.display = '';

	const close = dialog.querySelector('.closeButton');
	close.addEventListener('click', Module.gb_close_printer_dialog);

	const deleteAll = dialog.querySelector('.deleteAll');
	deleteAll.addEventListener('click', Module.gb_printer_delete_all);

	const downloadAll = dialog.querySelector('.downloadAll');
	downloadAll.addEventListener('click', Module.gb_printer_download_all);
}

Module.disable_workboy = () => {
	Module.hide_workboy_osk();

	let button = document.getElementById('workboyButton');
	if (button) {
		button.remove();
	}
}

Module.enable_workboy = () => {
	let button = document.getElementById('workboyButton');

	if (!button) {
		button = document.createElement('span');
		button.id = 'workboyButton';
		button.classList.add('button', 'small');
		button.innerHTML = '<svg><use href="img/workboy.svg#i"></use></svg>';

		button.addEventListener('click', event => {
			event.preventDefault();

			if (button.classList.contains('active')) {
				Module.hide_workboy_osk();
				button.classList.remove('active');
			}
			else {
				Module.open_workboy_osk();
				button.classList.add('active');
			}
		});

		document.getElementById('controls').appendChild(button);
	}
}

Module.hide_workboy_osk = () => {
	if (Module.workboy_osk === null) {
		return;
	}

	Module.workboy_osk.setOptions({
		theme: 'hg-theme-default'
	});
}

Module.workboy_osk = null;
Module.open_workboy_osk = async () => {
	if (Module.workboy_osk === null) {
		Module._pause();

		Module.setStatus('Loading On-Screen Keyboard (0/2)');
		const Keyboard = (await import('./js/simple-keyboard.min.js')).default;

		Module.setStatus('Loading On-Screen Keyboard (1/2)');
		if (!document.getElementById('simple-keyboard-css')) {
			await new Promise((resolve, reject) => {
				const link = document.createElement('link');
				link.setAttribute('id', 'simple-keyboard-css');
				link.setAttribute('rel', 'stylesheet');
				link.addEventListener('load', resolve);
				link.addEventListener('error', reject);
				link.setAttribute('href', './css/simple-keyboard.css');
				document.head.appendChild(link);
			});
		}
		Module.setStatus(null);

		const node = document.createElement('div');
		node.classList.add('workboy');
		const controls = document.getElementById('controls');
		controls.parentNode.insertBefore(node, controls.nextSibling);

		function get_event_options(button, keypress = false) {
			let options = {};

			if (button[0] == '{') {
				switch (button) {
					case '{esc}':    options = { key: 'Escape', charCode: 0, keyCode: 27, which: 27 }; break
					case '{del}':    options = { key: 'Delete', charCode: 0, keyCode: 46, which: 46 }; break

					case '{ret}':    options = { key: 'Enter', charCode: 0, keyCode: 13, which: 13 }; break
					case '{space}':  options = { key: ' ', charCode: 32, keyCode: 32, which: 32 }; break
					case '{clear}':  options = { key: 'M', charCode: 109, keyCode: 77, which: 77 }; break
					case '{MC}':     options = { key: 'U', charCode: 85, keyCode: 85, which: 85 }; break
					case '{MR}':     options = { key: 'Y', charCode: 89, keyCode: 89, which: 89 }; break
					case '{M+}':     options = { key: 'R', charCode: 82, keyCode: 82, which: 82 }; break
					case '{M-}':     options = { key: 'T', charCode: 84, keyCode: 84, which: 84 }; break
					case '{quotes}': options = { charCode: 96, keyCode: 192, which: 192 }; break
					case '{pound}':  options = { charCode: 126, keyCode: 126, which: 126 }; break
					case '{single_quote}': options = { charCode: 39, keyCode: 163, which: 163 }; break

					case '{plus}':     options = { key: '+', charCode: 43, keyCode: 43, which: 43 }; break
					case '{minus}':    options = { code: 'Slash', key: '-', charCode: 45, keyCode: 189, which: 189 }; break
					case '{multiply}': options = { code: 'NumpadMultiply', key: '*', charCode: 106, keyCode: 106, which: 106 }; break
					case '{divide}':   options = { code: 'NumpadDivide',   key: '/', charCode: 47, keyCode: 111, which: 111 }; break
					case '{period}':   options = { code: 'Period',   key: '.', charCode: 46, keyCode: 190, which: 190 }; break
					case '{decimal}':  options = { code: 'NumpadDecimal',   key: '.', charCode: 0, keyCode: 110, which: 110 }; break
					case '{percent}':  options = { key: '%', charCode: 37, keyCode: 53, which: 53 }; break
					case '{lparen}':  options = { key: '(', charCode: 40, keyCode: 56, which: 56 }; break
					case '{rparen}':  options = { key: ')', charCode: 41, keyCode: 57, which: 57 }; break

					case '{left}':  options = { key: 'ArrowLeft',  charCode: 0, keyCode: 37, which: 37 }; break;
					case '{up}':    options = { key: 'ArrowUp',    charCode: 0, keyCode: 38, which: 38 }; break;
					case '{right}': options = { key: 'ArrowRight', charCode: 0, keyCode: 39, which: 39 }; break;
					case '{down}':  options = { key: 'ArrowDown',  charCode: 0, keyCode: 40, which: 40 }; break;

					case '{clock}':      options = { key: 'F1',  charCode: 0, keyCode: 112, which: 112 }; break;
					case '{temps}':      options = { key: 'F2',  charCode: 0, keyCode: 113, which: 113 }; break;
					case '{money}':      options = { key: 'F3',  charCode: 0, keyCode: 114, which: 114 }; break;
					case '{calc}':       options = { key: 'F4',  charCode: 0, keyCode: 115, which: 115 }; break;
					case '{date}':       options = { key: 'F5',  charCode: 0, keyCode: 116, which: 116 }; break;
					case '{conversion}': options = { key: 'F6',  charCode: 0, keyCode: 117, which: 117 }; break;
					case '{records}':    options = { key: 'F7',  charCode: 0, keyCode: 118, which: 118 }; break;
					case '{world}':      options = { key: 'F8',  charCode: 0, keyCode: 119, which: 119 }; break;
					case '{phone}':      options = { key: 'F9',  charCode: 0, keyCode: 120, which: 120 }; break;
					case '{ins}':        options = { key: 'F10', charCode: 0, keyCode: 121, which: 121 }; break;

					case '{num}':  options = { key: 'Shift',   code: 'ShiftLeft',   charCode: 0, keyCode: 16, which: 16 }; break

					default:
						console.warn('Unhandled button:', button);
						return;
				}
			}
			else {
				const keyCode  = button.toUpperCase().charCodeAt(0);
				const charCode = button.charCodeAt(0);
				options = {
					key: button,
					charCode,
					keyCode,
					which: keyCode,
					shiftKey: Module.workboy_osk.layoutName == 'num'
				};
			}

			options.bubbles = true;
			return options;
		}

		const gb_pressed_keys = new Set();
		function onKeyPress(button) {
			if (gb_pressed_keys.has(button)) return;

			if (button === '{caps}' || button === '{num}') {
				const newLayout = button.slice(1, -1);
				const previousLayout = Module.workboy_osk.options.layoutName;

				Module.workboy_osk.setOptions({
					layoutName: newLayout,
				});

				// Handle the "shift" key
				if (newLayout == 'num') {
					Module.canvas.dispatchEvent(new KeyboardEvent('keydown', get_event_options('{num}')));
				}
				else {
					Module.canvas.dispatchEvent(new KeyboardEvent('keyup', get_event_options('{num}')));
				}

				return;
			}

			gb_pressed_keys.add(button);

			let options = get_event_options(button);
			Module.canvas.dispatchEvent(new KeyboardEvent('keydown', options));
			setTimeout(() => {
				Module.canvas.dispatchEvent(new KeyboardEvent('keyup', options));
				gb_pressed_keys.delete(button);
			}, 60);

			options = get_event_options(button, true);
			Module.canvas.dispatchEvent(new KeyboardEvent('keypress', options));
		}

		Module.workboy_osk = new Keyboard(node, {
			debug: Module.SAMEBOY_DEBUG,
			onKeyPress,

			theme: 'hg-theme-default show-workboy',
			disableButtonHold: true,
			disableCaretPositioning: true,
			maxLength: 1,
			layoutName: 'caps',

			layout: {
				'caps': [
					'{esc} {clock} {temps} {money} {calc} {date} {conversion} {records} {world} {phone} {del} {ins}',
					'Q W E R T Y U I O P $',
					'A S D F G H J K L ; {ret}',
					'{num} Z X C V B N M , [{period} {up} /]',
					'{caps} {quotes} {space} {single_quote} [{left} {down} {right}]'
				],
				'num': [
					'{esc} {clock} {temps} {money} {calc} {date} {conversion} {records} {world} {phone} {del} {ins}',
					'1 2 3 {M+} {M-} {MR} {MC} ! {pound} * #',
					'4 5 6 {plus} {minus} {multiply} {divide} {lparen} {rparen} : {ret}',
					'{num} 7 8 9 {decimal} = {percent} {clear} < [> {up} ?]',
					'{caps} 0 {space} @ [{left} {down} {right}]'
				],
			},

			display: {
				'{esc}': 'ESC',
				'{clock}': '<svg class="icon"><use href="img/workboy/clock.svg#i"></use></svg>',
				'{temps}': '<svg class="icon"><use href="img/workboy/temps.svg#i"></use></svg>',
				'{money}': '<svg class="icon"><use href="img/workboy/money.svg#i"></use></svg>',
				'{calc}': '<svg class="icon"><use href="img/workboy/calc.svg#i"></use></svg>',
				'{date}': '<svg class="icon"><use href="img/workboy/date.svg#i"></use></svg>',
				'{conversion}': '<svg class="icon"><use href="img/workboy/conversion.svg#i"></use></svg>',
				'{records}': '<svg class="icon"><use href="img/workboy/records.svg#i"></use></svg>',
				'{world}': '<svg class="icon"><use href="img/workboy/world.svg#i"></use></svg>',
				'{phone}': '<svg class="icon"><use href="img/workboy/phone.svg#i"></use></svg>',
				'{del}': 'DEL',
				'{ins}': 'INS',

				'{caps}': 'CAPS',
				'{num}': 'NUM',
				'{ret}': 'RTN',
				'{space}': 'SPACE',

				'{clear}': '🆑',
				'{MC}': 'MC',
				'{MR}': 'MR',
				'{M+}': 'M+',
				'{M-}': 'M-',
				'{quotes}': ',,',
				'{single_quote}': '\'',
				'{pound}': '£',

				'{plus}': '+',
				'{minus}': '–',
				'{multiply}': '×',
				'{divide}': '÷',
				'{period}': '.',
				'{decimal}': '.',
				'{percent}': '%',
				'{lparen}': '(',
				'{rparen}': ')',

				'{left}': '←',
				'{right}': '→',
				'{up}': '↑',
				'{down}': '↓',
			},

			buttonTheme: [
				{
					class: 'svgButton',
					buttons: '{clock} {temps} {money} {calc} {date} {conversion} {records} {world} {phone}'
				},
				{
					class: 'specialButton',
					buttons: '{esc} {del} {ins} {num} {caps} {ret} {up} {down} {left} {right}'
				}
			]
		});

		Module._resume();
	}
	else {
		Module.workboy_osk.setOptions({
			theme: 'hg-theme-default show-workboy'
		});
	}
}

window.enable_workboy = Module.enable_workboy;
window.disable_workboy = Module.disable_workboy;
window.open_osk = Module.open_workboy_osk; // TODO: REMOVE!!
window.close_osk = Module.close_workboy_osk; // TODO: REMOVE!!

Module.setStatus('Downloading ...');

window.onerror = () => {
	Module.setStatus('Exception thrown, see JavaScript console');
	spinnerElement.style.display = 'none';
	Module.setStatus = text => {
		if (text) Module.printErr('[post-exception status] ' + text);
	};
};

Module.ready.then(async () => {
	Module.setStatus('Syncing filesystem');

	FS.mkdir('/persist');
	FS.mount(IDBFS, { }, '/persist');

	await Module.gb_syncfs(true);

	// Call the exported init function
	Module._init();
	Module.setStatus(null);
});
