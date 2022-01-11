const frame_rate = (0x400000 / 70224.0);
const ms_per_frame = 1000 / frame_rate;
let last_frame_time = 0;

function stringHash(str) {
	let hash = 0;

	if (str.length === 0) {
		return hash;
	}

	for (let i = 0; i < str.length; i++) {
		let chr = str.charCodeAt(i);
		hash = ((hash << 5) - hash) + chr;
		hash |= 0; // Convert to 32bit integer
	}

	return hash;
}

function run_frame(time) {
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

async function loadRomFromMemory(name, data) {
	const pos = name.lastIndexOf('.');
	const battery_name = name.substr(0, pos < 0 ? name.length : pos) + '.sav';
	const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), 'i8', ALLOC_NORMAL);

	// Copy data into WASM memory
	const ptr = Module._malloc(data.byteLength);
	const wasm_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, data.byteLength);
	wasm_buf.set(new Uint8Array(data));

	Module._load_rom(wasm_buf.byteOffset, wasm_buf.byteLength, battery_path);

	window.requestAnimationFrame(run_frame);
}

async function loadROM(file) {
	const name = file.name;

	return new Promise((resolve, reject) => {
		const reader = new FileReader();

		reader.onload = () => {
			resolve(reader.result);
		}

		reader.onerror = reject;

		reader.readAsArrayBuffer(file);
	})
	.then(buffer => loadRomFromMemory(name, buffer));
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
	await loadRomFromMemory(name, buf);
}

async function handleFileSelect(event, files) {
	event.stopPropagation();
	event.preventDefault();

	if (files.length) {
		await loadROM(files[0]);
	}
}

function handleDragOver(event) {
	event.stopPropagation();
	event.preventDefault();
	event.dataTransfer.dropEffect = 'copy';
}

window.addEventListener('dragover', handleDragOver, false);

window.addEventListener('drop', e => {
	handleFileSelect(e, e.dataTransfer.files);
}, false);

document.getElementById('file').addEventListener('change', e => {
	handleFileSelect(e, e.target.files);
}, false);

async function romClickHandler(event) {
	event.stopPropagation();
	event.preventDefault();

	await loadRemoteRom(event.target.href);
}

for (const anchor of document.querySelectorAll('#demo-roms a')) {
	anchor.addEventListener('click', romClickHandler);
}
