const frame_rate = (0x400000 / 70224.0);
const ms_per_frame = 1000 / frame_rate;
let last_frame_time = 0;

const stringHash = str => {
	let hash = 0;

	if (str.length === 0) return hash;

	for (let i = 0; i < str.length; i++) {
		let chr = str.charCodeAt(i);
		hash = ((hash << 5) - hash) + chr;
		hash |= 0; // Convert to 32bit integer
	}

	return hash;
}

const run_frame = time => {
	window.requestAnimationFrame(run_frame);

	if (document.visibilityState) {
		if (document.visibilityState == "hidden") {
			return;
		}
	}
	else if (document.hidden) {
		return;
	}

	const delta = time - last_frame_time;

	if (delta > ms_per_frame) {
		Module._run_frame();

		last_frame_time = time - (delta % ms_per_frame);
	}
}

const loadRomFromMemory = (name, data) => {
	const pos = name.lastIndexOf('.');
	const battery_name = name.substr(0, pos < 0 ? name.length : pos) + '.sav';
	const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), 'i8', ALLOC_NORMAL);

	// Copy data into WASM memory
	const ptr = Module._malloc(data.byteLength);
	const wasm_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, data.byteLength);
	wasm_buf.set(new Uint8Array(data));

	Module._load_rom(wasm_buf.byteOffset, wasm_buf.byteLength, battery_path);

	window.requestAnimationFrame(run_frame)
}

const loadROM = f => {
	const reader = new FileReader();

	reader.onload = (file => {
		return event => {
			loadRomFromMemory(file.name, event.target.result)
		};
	})(f);

	reader.readAsArrayBuffer(f);
}

const loadRemoteRom = async url => {
	const request = new Request(url);

	const name = (_ => {
		const name = url.substring(url.lastIndexOf('/') + 1);

		if (name.endsWith('.gb') || name.endsWith('.gbc')) {
			return name
		}
		else if (name.length) {
			return `${name}.gb`
		}

		return stringHash(url)
	})()

	const response = await fetch(request);
	if (!response.ok) {
		throw new Error('HTTP error, status = ' + response.status);
	}

	const buf = await response.arrayBuffer();
	loadRomFromMemory(name, buf);
}

const handleFileSelect = (evt, files) => {
	evt.stopPropagation();
	evt.preventDefault();

	if (files.length) {
		loadROM(files[0]);
	}
}

const handleDragOver = evt => {
	evt.stopPropagation();
	evt.preventDefault();
	evt.dataTransfer.dropEffect = 'copy'; // Explicitly show this is a copy.
}

window.addEventListener('dragover', handleDragOver, false);

window.addEventListener('drop', e => {
	handleFileSelect(e, e.dataTransfer.files);
}, false);

document.getElementById('file').addEventListener('change', e => {
	handleFileSelect(e, e.target.files);
}, false);

Module.onRuntimeInitialized = _ => {
	FS.mkdir('/persist');
	FS.mount(IDBFS, { }, '/persist');

	FS.syncfs(true, function (err) {
		if (!err) {
			console.log('Successfully loaded FS from persistent storage')
		}
		else {
			console.error(err)
		}

		// Call the exported init function
		Module._init();
	})
};

const romClickHandler = event => {
	event.stopPropagation();
	event.preventDefault();
	loadRemoteRom(event.target.href);
}

for (const anchor of document.querySelectorAll('#demo-roms a')) {
	anchor.addEventListener('click', romClickHandler);
}
