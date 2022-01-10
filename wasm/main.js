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

	try {
		// try to create the virtual ROM folder
		FS.mkdir('/rom');
	} catch (e) { }

	try {
		// try to delete all previous ROM files
		for (let file of FS.readdir('/rom').filter(f => f != '.' && f != '..')) {
			FS.unlink(`/rom/${file}`)
		}
	} catch (e) { }

	// create a new virtual file from memory
	Module['FS_createDataFile']('/rom/', name, new Uint8Array(data), true, true);

	const rom_path = allocate(intArrayFromString(`/rom/${name}`), 'i8', ALLOC_NORMAL);
	const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), 'i8', ALLOC_NORMAL);

	Module._load_rom_from_file(rom_path, battery_path);

	// The ROM has been read into memory, we can unlink the file now
	FS.unlink(`/rom/${name}`)

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
	console.log(Module.get_models());
	console.log(Module.get_sgb_revisions());

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
