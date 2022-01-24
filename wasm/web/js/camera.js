export default Module => {
	let unsupported = false;
	let ready = false;
	let buf = null;
	let video = null;
	let canvas = null;
	let facing_mode = 'environment';

	const Camera = { };

	Camera.stop = () => {
		if (video && video.srcObject) {
			if (video.srcObject.stop) {
				video.srcObject.stop();
			}
			else if (video.srcObject.getTracks) {
				video.srcObject.getTracks().forEach(track => track.stop());
			}

			video.srcObject = null;
		}
	}

	Camera.remove = () => {
		Camera.stop();

		if (video) {
			video.remove();
			video = null;
		}

		if (canvas) {
			canvas.remove();
			canvas = null;
		}

		ready = false;
		buf = null;
	}

	Camera.start_capture = () => {
		ready = false;

		navigator.mediaDevices
			.getUserMedia({
				audio: false,
				video: {
					width: { min: 128, ideal: 128 },
					height: { min: 112, ideal: 112 },
					facingMode: facing_mode,
					frameRate: { ideal: 20 },
				}
			})
			.then(stream => {
				video.srcObject = stream;
				video.onloadedmetadata = () => video.play();
			})
			.catch(err => {
				console.error('getUserMedia() failed:', err);
				unsupported = true;
			});
	}

	Camera.canplay = () => {
		if (ready) {
			return;
		}

		let width = 128;
		let height = Math.round(video.videoHeight / (video.videoWidth / width));

		if (height < 112) {
			height = 112;
			width = Math.max(
				128,
				Math.round(video.videoWidth / (video.videoHeight / height))
			);
		}

		console.log(`Camera input resolution: ${video.videoWidth}x${video.videoHeight}`);

		video.setAttribute('width', width);
		video.setAttribute('height', height);

		canvas.setAttribute('width', width);
		canvas.setAttribute('height', height);

		const size = canvas.width * canvas.height * 4;
		const ptr = Module._malloc(size);

		buf = new Uint8Array(Module.HEAPU8.buffer, ptr, size);

		Module._camera_set_buf(ptr, size, width, height);
		ready = true;

		const stream = video.srcObject;
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

					Camera.stop();
					Camera.start_capture();
				});

				document.getElementById('controls').appendChild(flip_button);
			}

			facing_mode = flip_button.dataset.facingMode;
		}
		else if (flip_button) {
			flip_button.remove();
			flip_button = null;
			facing_mode = undefined;
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

	Camera.init = () => {
		if (unsupported) {
			return -1;
		}

		try {
			if (!canvas) {
				canvas = document.createElement('canvas');
			}

			if (!video) {
				video = document.createElement('video');

				video.addEventListener('canplay', Camera.canplay, false);

				Camera.start_capture();
			}

			if (ready) {
				const ctx = canvas.getContext('2d');
				ctx.drawImage(video, 0, 0, canvas.width, canvas.height);

				const iDat = ctx.getImageData(0, 0, canvas.width, canvas.height);
				buf.set(new Uint8Array(iDat.data));

				// Ready
				return 0;
			}

			// Not yet ready
			return 1;
		}
		catch (err) {
			console.error("Failed to get camera stream:", err);

			// Error
			return -1;
		}
	}

	return Camera;
}
