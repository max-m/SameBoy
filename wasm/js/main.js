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

function simulate_key_event(type, name) {
	const e = new Event(type, { bubbles: true });

	const key = Module.gb_touch_keymap[name];

	// SDL2 uses `keyCode`, see:
	// https://github.com/emscripten-ports/SDL2/blob/c180eca6734d9520e1ef3c8643063b62b9f559b4/src/video/emscripten/SDL_emscriptenevents.c#L499-L500
	e.keyCode = key.keyCode;
	e.which   = key.keyCode;

	Module.canvas.dispatchEvent(e);
}

async function handle_file_select(event) {
	event.stopPropagation();
	event.preventDefault();

	const files = (event.dataTransfer || event.target).files;

	if (files.length) {
		await Module.gb_open_file(event);
	}
}

function handle_drag_over(event) {
	event.stopPropagation();
	event.preventDefault();
	event.dataTransfer.dropEffect = 'copy';
}

async function rom_click_handler(event) {
	event.stopPropagation();
	event.preventDefault();

	await load_remote_rom(event.target.href);
}

function setup_dpad() {
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
				simulate_key_event(eventType, 'up');
			break;

			case down:
				simulate_key_event(eventType, 'down');
			break;

			case left:
				simulate_key_event(eventType, 'left');
			break;

			case right:
				simulate_key_event(eventType, 'right');
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

function setup_simple_button(button, name) {
	function activate(event) {
		event.preventDefault();
		button.setPointerCapture(event.pointerId);

		button.classList.add('active');
		simulate_key_event('keydown', name);
	}

	function deactivate(event) {
		event.preventDefault();
		button.releasePointerCapture(event.pointerId);

		button.classList.remove('active');
		simulate_key_event('keyup', name);
	}

	button.addEventListener('pointerdown',   activate);
	button.addEventListener('pointerup',     deactivate);
	button.addEventListener('pointercancel', deactivate);

	button.setAttribute('draggable', 'false');
	button.setAttribute('unselectable', 'on');
}

function setup_controls() {
	setup_dpad();

	setup_simple_button(document.getElementById('startButton'), 'start');
	setup_simple_button(document.getElementById('selectButton'), 'select');

	setup_simple_button(document.getElementById('aButton'), 'a');
	setup_simple_button(document.getElementById('bButton'), 'b');

	setup_simple_button(document.getElementById('menuButton'), 'menu');

	const fsButton = document.getElementById('fullscreenButton');

	if (document.fullscreenEnabled) {
		document.addEventListener('fullscreenchange', (event) => {
			if (document.fullscreenElement) {
				fsButton.classList.add('active');
			}
			else {
				fsButton.classList.remove('active');
			}

			setTimeout(() => {
				window.dispatchEvent(new Event('resize'));
			}, 32);
		});

		document.addEventListener('fullscreenerror', event => {
			console.error('Failed to go fullscreen:', event);
		});

		fsButton.addEventListener('click', () => {
			if (document.fullscreenElement) {
				document.exitFullscreen();
			}
			else {
				document.getElementById('system').requestFullscreen();
			}
		});
	}
	else {
		fsButton.style.display = 'none';
	}
}

function startup() {
	window.addEventListener('dragover', handle_drag_over, false);
	window.addEventListener('drop', handle_file_select, false);

	setup_controls();

	document.addEventListener('visibilitychange', async () => {
		if (document.visibilityState == 'visible') {
			Module._resume();
		}
		else {
			Module._pause();
		}

		Module._save_battery();
		await Module.gb_syncfs();
	});
}

if (document.readyState !== 'loading') {
	setTimeout(startup, 0);
}
else {
	window.addEventListener('DOMContentLoaded', startup);
}
