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

Module.sameboy_syncfs_in_progress = false;
Module.sameboy_syncfs_needs_sync = false;
Module.sameboy_syncfs = async function (populate = false) {
	if (Module.sameboy_syncfs_in_progress) {
		Module.sameboy_syncfs_needs_sync = { populate };
		return;
	}
	Module.sameboy_syncfs_in_progress = true;
	console.log("Syncing file system ...");

	new Promise((resolve, reject) => {
		FS.syncfs(populate, function (err) {
			Module.sameboy_syncfs_in_progress = false;

			if (err) {
				reject(err);
			}
			else if (Module.sameboy_syncfs_needs_sync) {
				console.log("A sync was requested while syncing, syncing again.");
				Module.sameboy_syncfs_needs_sync = undefined;

				Module.sameboy_syncfs(Module.sameboy_syncfs_needs_sync.populate)
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

	return Module.sameboy_syncfs_promise;
};

Module.onRuntimeInitialized = async () => {
	FS.mkdir('/persist');
	FS.mount(IDBFS, { }, '/persist');

	await Module.sameboy_syncfs(true);

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
