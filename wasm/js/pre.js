const statusElement = document.getElementById('status');
const progressElement = document.getElementById('progress');
const spinnerElement = document.getElementById('spinner');

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

Module.setStatus = function(text) {
	if (!Module.setStatus.last) {
		Module.setStatus.last = {
			time: Date.now(),
			text: ''
		};
	}

	if (text === Module.setStatus.last.text) {
		return;
	}

	const m = text.match(/([^(]+)\((\d+(\.\d+)?)\/(\d+)\)/);
	const now = Date.now();

	// if this is a progress update, skip it if too soon
	if (m && now - Module.setStatus.last.time < 30) {
		return;
	}

	Module.setStatus.last.time = now;
	Module.setStatus.last.text = text;

	if (m) {
		text = m[1];
		progressElement.value = parseInt(m[2]) * 100;
		progressElement.max = parseInt(m[4]) * 100;
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
	wasm_buf.set(new Uint8Array(data));

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

	const response = await fetch(request);
	if (!response.ok) {
		throw new Error('HTTP error, status = ' + response.status);
	}

	const buf = await response.arrayBuffer();
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

Module.gb_camera_ready = false;
Module.gb_camera_buf = null;
Module.gb_camera_video = null;
Module.gb_camera_canvas = null;
Module.gb_camera_facing_mode = 'environment';

Module.gb_camera_stop = () => {
	if (Module.gb_camera_video && Module.gb_camera_video.srcObject) {
		if (Module.gb_camera_video.srcObject.stop) {
			Module.gb_camera_video.srcObject.stop();
		}
		else if (Module.gb_camera_video.srcObject.getTracks) {
			Module.gb_camera_video.srcObject.getTracks().forEach(track => track.stop());
		}

		Module.gb_camera_video.srcObject = null;
	}
}

Module.gb_camera_remove = () => {
	Module.gb_camera_stop();

	if (Module.gb_camera_video) {
		Module.gb_camera_video.remove();
		Module.gb_camera_video = null;
	}

	if (Module.gb_camera_canvas) {
		Module.gb_camera_canvas.remove();
		Module.gb_camera_canvas = null;
	}

	Module.gb_camera_ready = false;
	Module.gb_camera_buf = null;
}

Module.gb_camera_start_capture = () => {
	Module.gb_camera_ready = false;

	navigator.mediaDevices
		.getUserMedia({
			audio: false,
			video: {
				width: { min: 128, ideal: 128 },
				height: { min: 112, ideal: 112 },
				facingMode: Module.gb_camera_facing_mode,
				frameRate: { ideal: 20 },
			}
		})
		.then(stream => {
			Module.gb_camera_video.srcObject = stream;
			Module.gb_camera_video.onloadedmetadata = () => Module.gb_camera_video.play();
		})
		.catch(err => {
			console.error('getUserMedia() failed:', err);
			Module._camera_unsupported();
		});
}

Module.gb_camera_canplay = () => {
	if (Module.gb_camera_ready) {
		return;
	}

	let width = 128;
	let height = Math.round(Module.gb_camera_video.videoHeight / (Module.gb_camera_video.videoWidth / width));

	if (height < 112) {
		height = 112;
		width = Math.max(
			128,
			Math.round(Module.gb_camera_video.videoWidth / (Module.gb_camera_video.videoHeight / height))
		);
	}

	console.log(`Camera input resolution: ${Module.gb_camera_video.videoWidth}x${Module.gb_camera_video.videoHeight}`);

	Module.gb_camera_video.setAttribute('width', width);
	Module.gb_camera_video.setAttribute('height', height);

	Module.gb_camera_canvas.setAttribute('width', width);
	Module.gb_camera_canvas.setAttribute('height', height);

	const size = Module.gb_camera_canvas.width * Module.gb_camera_canvas.height * 4;
	const ptr = Module._malloc(size);

	Module.gb_camera_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, size);

	Module._camera_set_buf(ptr, size, width, height);
	Module.gb_camera_ready = true;

	const stream = Module.gb_camera_video.srcObject;
	if (!stream) {
		return;
	}

	const track = stream.getVideoTracks()[0];

	if (!track) {
		return;
	}

	let flip_button = document.getElementById('cameraFlipButton');

	const camera_supported_constraints = navigator.mediaDevices.getSupportedConstraints();
	const track_capabilities = track.getCapabilities();

	if (camera_supported_constraints.facingMode) {
		if (!flip_button) {
			flip_button = document.createElement('span');
			flip_button.id = 'cameraFlipButton';
			flip_button.classList.add('button', 'small');
			flip_button.innerHTML = '<svg><use href="img/flip.svg#i"></use></svg>';
			flip_button.dataset.facingMode = 'environment';

			flip_button.addEventListener('click', event => {
				event.preventDefault();

				if (flip_button.dataset.facingMode == 'environment') {
					flip_button.dataset.facingMode = 'user';
				}
				else {
					flip_button.dataset.facingMode = 'environment';
				}

				Module.gb_camera_stop();
				Module.gb_camera_start_capture();
			});

			document.getElementById('controls').appendChild(flip_button);
		}

		Module.gb_camera_facing_mode = flip_button.dataset.facingMode;
	}
	else if (flip_button) {
		flip_button.remove();
		flip_button = null;
		Module.gb_camera_facing_mode = undefined;
	}

	let torch_button = document.getElementById('cameraTorchButton');

	if (track_capabilities.torch) {
		if (!torch_button) {
			torch_button = document.createElement('span');
			torch_button.id = 'cameraTorchButton';
			torch_button.classList.add('button', 'small');
			torch_button.innerHTML = '<svg><use href="img/torch.svg#i"></use></svg>';
			torch_button.dataset.torch = 'false';

			torch_button.addEventListener('click', event => {
				event.preventDefault();

				if (torch_button.dataset.torch == 'true') {
					torch_button.dataset.torch = 'false';
				}
				else {
					torch_button.dataset.torch = 'true';
				}

				track.applyConstraints({
					advanced: [{
						torch: torch_button.dataset.torch == 'true'
					}]
				});
			});

			document.getElementById('controls').appendChild(torch_button);
		}
	}
	else if (torch_button) {
		torch_button.remove();
		torch_button = null;
	}
}

Module.gb_camera_init = () => {
	try {
		if (!Module.gb_camera_canvas) {
			Module.gb_camera_canvas = document.createElement('canvas');
		}

		if (!Module.gb_camera_video) {
			Module.gb_camera_video = document.createElement('video');

			Module.gb_camera_video.addEventListener('canplay', Module.gb_camera_canplay, false);

			Module.gb_camera_start_capture();
		}

		if (Module.gb_camera_ready) {
			const ctx = Module.gb_camera_canvas.getContext('2d');
			ctx.drawImage(Module.gb_camera_video, 0, 0, Module.gb_camera_canvas.width, Module.gb_camera_canvas.height);

			const iDat = ctx.getImageData(0, 0, Module.gb_camera_canvas.width, Module.gb_camera_canvas.height);
			Module.gb_camera_buf.set(new Uint8Array(iDat.data));

			return 0;
		}

		return 1;
	}
	catch (err) {
		console.error("Failed to get camera stream:", err);
		return 2;
	}
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
