const frame_rate = (0x400000 / 70224.0);
const ms_per_frame = 1000 / frame_rate;
let last_frame_time = 0;
let animation_frame_handle = undefined;

function string_hash(str) {
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

function simulate_key_event(type, key, which) {
	const e = new Event(type, { bubbles: true });

	e.code = key;
	e.key = key;
	e.keyCode = which;
	e.which = which;

	Module.canvas.dispatchEvent(e);
}

async function loadRomFromMemory(name, data) {
	document.body.dispatchEvent(new Event('click'));

	const pos = name.lastIndexOf('.');
	const battery_name = name.substr(0, pos < 0 ? name.length : pos) + '.sav';
	const battery_path = allocate(intArrayFromString(`/persist/${battery_name}`), ALLOC_NORMAL);

	// Copy data into WASM memory
	const ptr = Module._malloc(data.byteLength);
	const wasm_buf = new Uint8Array(Module.HEAPU8.buffer, ptr, data.byteLength);
	wasm_buf.set(new Uint8Array(data));

	Module._load_rom(wasm_buf.byteOffset, wasm_buf.byteLength, battery_path);
}

async function loadROM(file) {
	const name = file.name;

	const buffer = await new Promise((resolve, reject) => {
		const reader = new FileReader();

		reader.onload = () => {
			resolve(reader.result);
		}

		reader.onerror = reject;

		reader.readAsArrayBuffer(file);
	});

	return await loadRomFromMemory(name, buffer);
}

async function loadRemoteRom(url) {
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

async function romClickHandler(event) {
	event.stopPropagation();
	event.preventDefault();

	await loadRemoteRom(event.target.href);
}

function setupDpad() {
	const dpad  = document.getElementById('dpad');

	const up    = document.getElementById('upButton');
	const down  = document.getElementById('downButton');
	const left  = document.getElementById('leftButton');
	const right = document.getElementById('rightButton');

	let isActive = false;

	let isLeft  = false;
	let isRight = false;
	let isUp    = false;
	let isDown  = false;

	function dispatch(button, active) {
		button.classList[active ? 'add' : 'remove']('active');

		const eventType = active ? 'keydown' : 'keyup';

		switch (button) {
			case up:
				simulate_key_event(eventType, 'ArrowUp', 38);
			break;

			case down:
				simulate_key_event(eventType, 'ArrowDown', 40);
			break;

			case left:
				simulate_key_event(eventType, 'ArrowLeft', 37);
			break;

			case right:
				simulate_key_event(eventType, 'ArrowRight', 39);
			break;
		}
	}

	function update(x = 0, y = 0, bounds) {
		const xRel = x / bounds.width;
		const yRel = y / bounds.height;

		const isLeftNew  = xRel <= 0.3;
		const isRightNew = xRel >= 0.7;
		const isUpNew    = yRel <= 0.3;
		const isDownNew  = yRel >= 0.7;

		if (isLeft != isLeftNew) {
			isLeft = isLeftNew;
			dispatch(left, isLeft);
		}

		if (isRight != isRightNew) {
			isRight = isRightNew;
			dispatch(right, isRight);
		}

		if (isUp != isUpNew) {
			isUp = isUpNew;
			dispatch(up, isUp);
		}

		if (isDown != isDownNew) {
			isDown = isDownNew;
			dispatch(down, isDown);
		}
	}

	function activate(event) {
		event.preventDefault();
		dpad.setPointerCapture(event.pointerId);

		isActive = true;

		const bounds = dpad.getBoundingClientRect();
		const x = event.clientX - bounds.x;
		const y = event.clientY - bounds.y;
		update(x, y, bounds);
	}

	function deactivate(event) {
		event.preventDefault();
		dpad.releasePointerCapture(event.pointerId);

		isActive = false;
		isLeft   = false;
		isRight  = false;
		isUp     = false;
		isDown   = false;

		dispatch(left, false);
		dispatch(right, false);
		dispatch(up, false);
		dispatch(down, false);
	}

	function move(event) {
		event.preventDefault();

		if (isActive) {
			const bounds = dpad.getBoundingClientRect();
			const x = event.clientX - bounds.x;
			const y = event.clientY - bounds.y;
			update(x, y, bounds);
		}
	}

	dpad.addEventListener('pointerdown',   activate);
	dpad.addEventListener('pointerup',     deactivate);
	dpad.addEventListener('pointercancel', deactivate);
	dpad.addEventListener('pointermove',   move);

	[ dpad, up, down, left, right ].forEach(button => {
		button.setAttribute('draggable', 'false');
		button.setAttribute('unselectable', 'on');
	});
}

function setupSimpleButton(button, key, which) {
	function activate(event) {
		event.preventDefault();
		button.setPointerCapture(event.pointerId);

		button.classList.add('active');
		simulate_key_event('keydown', key, which);
	}

	function deactivate(event) {
		event.preventDefault();
		button.releasePointerCapture(event.pointerId);

		button.classList.remove('active');
		simulate_key_event('keyup', key, which);
	}

	button.addEventListener('pointerdown',   activate);
	button.addEventListener('pointerup',     deactivate);
	button.addEventListener('pointercancel', deactivate);

	button.setAttribute('draggable', 'false');
	button.setAttribute('unselectable', 'on');
}

function setupControls() {
	setupDpad();

	setupSimpleButton(document.getElementById('startButton'), 'Enter', 13);
	setupSimpleButton(document.getElementById('selectButton'), 'Backspace', 8);

	setupSimpleButton(document.getElementById('aButton'), 'x', 88);
	setupSimpleButton(document.getElementById('bButton'), 'z', 90);
}

function startup() {
	window.addEventListener('dragover', handleDragOver, false);

	window.addEventListener('drop', e => {
		handleFileSelect(e, e.dataTransfer.files);
	}, false);

	document.getElementById('file').addEventListener('change', e => {
		handleFileSelect(e, e.target.files);
	}, false);

	for (const anchor of document.querySelectorAll('#demo-roms a')) {
		anchor.addEventListener('click', romClickHandler);
	}

	document.getElementById('menuButton').addEventListener('click', async event => {
		event.preventDefault();
		event.stopPropagation();

		Module._pause();

		const menuButton = event.target;
		const menu = document.getElementById('menu');

		menuButton.classList.add('active');
		menu.style.display = 'block';

		function onOutsideClick(event) {
			let node = event.target;

			while (node) {
				if (node == menu) {
					return;
				}

				node = node.parentElement;
			}

			document.body.removeEventListener('click', onOutsideClick);

			menuButton.classList.remove('active');
			menu.style.display = '';

			Module._resume();
		}

		document.body.addEventListener('click', onOutsideClick);

		Module._save_battery();
		await Module.sameboy_syncfs();
	});

	document.getElementById('synchronize').addEventListener('click', async event => {
		event.preventDefault();

		Module._save_battery();
		await Module.sameboy_syncfs();
	});

	setupControls();

	document.addEventListener('visibilitychange', async () => {
		Module._save_battery();
		await Module.sameboy_syncfs();
	});
}

if (document.readyState !== 'loading') {
	setTimeout(startup, 0);
}
else {
	window.addEventListener('DOMContentLoaded', startup);
}
