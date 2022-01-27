const VIRTUAL_RIGHT = 0;
const VIRTUAL_LEFT = 1;
const VIRTUAL_UP = 2;
const VIRTUAL_DOWN = 3;
const VIRTUAL_A = 4;
const VIRTUAL_B = 5;
const VIRTUAL_SELECT = 6;
const VIRTUAL_START = 7;
const VIRTUAL_TURBO = 8;
const VIRTUAL_REWIND = 9;
const VIRTUAL_SLOWMOTION = 10;
const VIRTUAL_MENU = 11;

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

		switch (button) {
			case up:
				Module._dispatch_virtual_key_event(VIRTUAL_UP, active);
			break;

			case down:
				Module._dispatch_virtual_key_event(VIRTUAL_DOWN, active);
			break;

			case left:
				Module._dispatch_virtual_key_event(VIRTUAL_LEFT, active);
			break;

			case right:
				Module._dispatch_virtual_key_event(VIRTUAL_RIGHT, active);
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

		if (isLeft) {
			isLeft = false;
			dispatch(left, false);
		}

		if (isRight) {
			isRight = false;
			dispatch(right, false);
		}

		if (isUp) {
			isUp = false;
			dispatch(up, false);
		}

		if (isDown) {
			isDown = false;
			dispatch(down, false);
		}
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

function setup_simple_button(button, key) {
	function activate(event) {
		event.preventDefault();
		button.setPointerCapture(event.pointerId);

		button.classList.add('active');
		Module._dispatch_virtual_key_event(key, true);
	}

	function deactivate(event) {
		event.preventDefault();
		button.releasePointerCapture(event.pointerId);

		button.classList.remove('active');
		Module._dispatch_virtual_key_event(key, false);
	}

	button.addEventListener('pointerdown',   activate);
	button.addEventListener('pointerup',     deactivate);
	button.addEventListener('pointercancel', deactivate);

	button.setAttribute('draggable', 'false');
	button.setAttribute('unselectable', 'on');
}

function setup_controls() {
	setup_dpad();

	setup_simple_button(document.getElementById('startButton'), VIRTUAL_START);
	setup_simple_button(document.getElementById('selectButton'), VIRTUAL_SELECT);

	setup_simple_button(document.getElementById('aButton'), VIRTUAL_A);
	setup_simple_button(document.getElementById('bButton'), VIRTUAL_B);

	setup_simple_button(document.getElementById('menuButton'), VIRTUAL_MENU);

	const fsButton = document.getElementById('fullscreenButton');

	if (document.fullscreenEnabled) {
		document.addEventListener('fullscreenchange', (event) => {
			if (document.fullscreenElement) {
				fsButton.classList.add('active');
			}
			else {
				fsButton.classList.remove('active');
			}

			// This should make sure that this function gets processed after
			// emscripten’s own fullscreenchange listener in any case
			requestAnimationFrame(() => {
				// Remove the SDL_WINDOW_FULLSCREEN flag if it has been set for some unknown reason
				Module._emscripten_sdl2_fullscreen_workaround();

				window.dispatchEvent(new Event('resize'))
			});
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
