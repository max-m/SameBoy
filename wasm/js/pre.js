const statusElement = document.getElementById('status');
const progressElement = document.getElementById('progress');
const spinnerElement = document.getElementById('spinner');
const canvas = document.getElementById('canvas');

// As a default initial behavior, pop up an alert when webgl context is lost. To make your
// application robust, you may want to override this behavior before shipping!
// See http://www.khronos.org/registry/webgl/specs/latest/1.0/#5.15.2
canvas.addEventListener("webglcontextlost", function(e) {
	e.preventDefault();
	alert('WebGL context lost. You will need to reload the page.');
}, false);

Module.logReadFiles = true;

Module.printWithColors = true;

Module.print = function() {
	console.log.apply(console, arguments);
};

Module.printErr = function() {
	console.error.apply(console, arguments);
};

Module.canvas = canvas;

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

Module.onRuntimeInitialized = async () => {
	FS.mkdir('/persist');
	FS.mount(IDBFS, { }, '/persist');

	await Module.gb_syncfs(true);

	// Call the exported init function
	Module._init();
};

Module.setStatus('Downloading...');

window.onerror = function() {
	Module.setStatus('Exception thrown, see JavaScript console');
	spinnerElement.style.display = 'none';
	Module.setStatus = function(text) {
		if (text) Module.printErr('[post-exception status] ' + text);
	};
};
