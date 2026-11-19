'use strict';
'require view';
'require rpc';
'require poll';
'require request';

var methods = {
	status: [], browse: [ 'path', 'offset', 'limit' ], play: [ 'path' ],
	pause: [ 'paused' ], stop: [], seek: [ 'seconds', 'relative' ],
	volume: [ 'value', 'mute' ], tracks: [ 'type', 'id' ],
	queue: [ 'action', 'path' ], profile: [ 'name' ],
	storage: [], remove: [ 'path' ], upload_begin: [ 'path', 'size' ], upload_cancel: [ 'token' ]
};
var api = {};
Object.keys(methods).forEach(function(name) {
	api[name] = rpc.declare({ object: 'airbridge.media', method: name, params: methods[name], reject: true });
});

/* Client-side checks improve feedback only. The controller must enforce its
 * configured library root, symlink confinement and argument allowlists. */
function relativePath(path, rootAllowed) {
	return typeof path === 'string' && path.length < 1024 &&
		(rootAllowed && path === '' || path !== '' && path.charAt(0) !== '/' &&
		!/[\x00-\x1f\x7f\\:]/.test(path) &&
		path.split('/').every(function(part) { return part !== '' && part !== '.' && part !== '..'; }));
}

function text(node, value) {
	node.textContent = value == null ? '' : String(value);
}

function errorText(error) {
	if (error && typeof error === 'object')
		return String(error.message || error.error || _('RPC request failed'));
	return String(error || _('RPC request failed'));
}

function checked(result, status) {
	if (!result || typeof result !== 'object' || Array.isArray(result))
		throw new Error(_('Invalid response from media controller'));
	if (result.ok !== true || result.error && !status)
		throw new Error(errorText(result.error));
	return result;
}

function checkedStatus(result) {
	checked(result, true);
	if ([ 'idle', 'playing', 'paused', 'unavailable' ].indexOf(result.state) === -1 ||
		typeof result.available !== 'boolean' || !result.capabilities ||
		typeof result.capabilities !== 'object' || Array.isArray(result.capabilities))
		throw new Error(_('Invalid status from media controller'));
	return result;
}

function clock(value) {
	if (typeof value !== 'number' || !isFinite(value) || value < 0)
		return '—';
	var seconds = Math.floor(value);
	return Math.floor(seconds / 3600) + ':' +
		('0' + Math.floor(seconds / 60) % 60).slice(-2) + ':' + ('0' + seconds % 60).slice(-2);
}

function bytes(value) {
	if (!Number.isSafeInteger(value) || value < 0)
		return '—';
	var units = [ 'B', 'KiB', 'MiB', 'GiB', 'TiB' ], size = value, unit = 0;
	while (size >= 1024 && unit < units.length - 1) {
		size /= 1024;
		unit++;
	}
	return (unit ? size.toFixed(2) : String(size)) + ' ' + units[unit];
}

function uploadName(name) {
	return typeof name === 'string' && name.charAt(0) !== '.' && name.indexOf('/') === -1 &&
		relativePath(name, false) && /\.(mkv|webm|mp4|m4v|mov|avi|ts|m2ts|mpg|mpeg|mp3|flac|wav|ogg|opus|aac)$/i.test(name);
}

return view.extend({
	handleSave: null,
	handleSaveApply: null,
	handleReset: null,

	load: function() {
		return api.status().then(checkedStatus).catch(function(error) {
			return { state: 'unavailable', available: false, capabilities: {}, error: errorText(error) };
		});
	},

	render: function(initial) {
		var self = this;
		this.status = initial;
		this.path = '';
		this.offset = 0;
		this.limit = 50;
		this.busy = false;
		this.browseSerial = 0;
		this.pageHistory = {};
		this.fileBusy = false;
		this.selectedFiles = [];
		this.storageInfo = null;
		this.transfer = null;
		this.controls = [];
		this.writable = L.hasViewPermission();

		function button(label, capability, handler, readOnly) {
			var node = E('button', { 'class': 'cbi-button', 'type': 'button',
				'click': function() { return handler(); } }, [ label ]);
			self.controls.push({ node: node, capability: capability, readOnly: !!readOnly });
			return node;
		}
		function input(type, label, attrs) {
			var node = E('input', Object.assign({ 'type': type, 'aria-label': label }, attrs || {}));
			return node;
		}
		function register(node, capability) {
			self.controls.push({ node: node, capability: capability, readOnly: false });
			return node;
		}
		function seek(seconds, relative) {
			if (!isFinite(seconds) || Math.abs(seconds) > 31536000 || (!relative && seconds < 0))
				return self.fail(_('Enter a valid seek time in seconds.'));
			return self.command('seek', [ seconds, relative ]);
		}

		this.notice = E('p', { 'role': 'status', 'aria-live': 'polite' });
		this.engineError = E('p', { 'role': 'status' });
		this.capabilityNotice = E('p');
		this.failure = E('p', { 'role': 'alert', 'class': 'error' });
		this.title = E('h3');
		this.telemetry = E('dl', { 'class': 'ab-media-telemetry' });
		this.pause = button(_('Pause'), 'pause', function() {
			return self.command('pause', [ self.status.state !== 'paused' ]);
		});
		this.seekInput = register(input('number', _('Seek time in seconds'), { min: 0, max: 31536000, step: 1, value: 0 }), 'seek');
		this.volume = register(input('range', _('Volume'), { min: 0, max: 100, step: 1, value: 50 }), 'volume');
		this.volume.addEventListener('change', function() {
			return self.command('volume', [ Number(self.volume.value), !!self.status.mute ]);
		});
		this.volumeLabel = E('span');
		this.mute = register(input('checkbox', _('Mute')), 'mute');
		this.mute.addEventListener('change', function() {
			return self.command('volume', [ Number(self.volume.value), self.mute.checked ]);
		});
		this.audio = register(E('select', { 'aria-label': _('Audio track') }), 'tracks');
		this.subtitles = register(E('select', { 'aria-label': _('Subtitle track') }), 'tracks');
		[ [ this.audio, 'audio' ], [ this.subtitles, 'sub' ] ].forEach(function(pair) {
			pair[0].addEventListener('change', function() {
				var id = Number(pair[0].value);
				if (Number.isInteger(id))
					return self.command('tracks', [ pair[1], id ]);
			});
		});

		this.breadcrumb = E('p');
		this.browseNotice = E('p', { 'role': 'status' });
		this.library = E('div', { 'class': 'ab-media-library' });
		this.previous = button(_('Previous page'), 'browse', function() {
			var earlier = (self.pageHistory[self.path] || []).filter(function(offset) { return offset < self.offset; });
			return self.browse(self.path, earlier.length ? earlier[earlier.length - 1] : 0);
		}, true);
		this.next = button(_('Next page'), 'browse', function() {
			return self.browse(self.path, self.nextOffset);
		}, true);
		this.parent = button(_('Parent directory'), 'browse', function() {
			return self.browse(self.path.split('/').slice(0, -1).join('/'), 0);
		}, true);
		this.queue = E('ol', { 'class': 'ab-media-queue' });
		this.storageNotice = E('p', { 'role': 'status' });
		this.selectionNotice = E('p');
		this.uploadNotice = E('p', { 'role': 'status', 'aria-live': 'polite' });
		this.uploadProgress = E('progress', { max: 100, value: 0, 'aria-label': _('Upload progress') });
		this.filePicker = E('input', {
			type: 'file', multiple: 'multiple', 'aria-label': _('Select media files'),
			accept: '.mkv,.webm,.mp4,.m4v,.mov,.avi,.ts,.m2ts,.mpg,.mpeg,.mp3,.flac,.wav,.ogg,.opus,.aac',
			change: function() {
				self.selectedFiles = Array.prototype.slice.call(self.filePicker.files || []);
				self.updateFileSelection();
			}
		});
		this.uploadButton = E('button', { type: 'button', 'class': 'cbi-button cbi-button-action',
			click: function() { return self.uploadSelected(); } }, [ _('Upload selected files') ]);
		this.cancelUploadButton = E('button', { type: 'button', 'class': 'cbi-button',
			click: function() { return self.cancelUpload(); } }, [ _('Cancel upload') ]);
		this.refreshStorageButton = E('button', { type: 'button', 'class': 'cbi-button',
			click: function() { return self.refreshStorage(); } }, [ _('Refresh storage') ]);
		var root = E('div', { 'class': 'ab-media' }, [
			E('style', {}, [
				'.ab-media{max-width:75rem}.ab-media-row{display:flex;flex-wrap:wrap;align-items:center;gap:.6rem;margin:.8rem 0}' +
				'.ab-media-row input[type=number]{width:8rem}.ab-media-row select{max-width:100%}' +
				'.ab-media-entry{display:flex;flex-wrap:wrap;align-items:center;gap:.5rem;padding:.5rem 0;border-bottom:1px solid #8884}' +
				'.ab-media-name{flex:1 1 14rem;overflow-wrap:anywhere}.ab-media-telemetry{display:grid;grid-template-columns:minmax(7rem,1fr) minmax(0,3fr);gap:.3rem}' +
				'.ab-media-telemetry dd{margin:0;overflow-wrap:anywhere}.ab-media-queue{overflow-wrap:anywhere}' +
				'.ab-media p{overflow-wrap:anywhere}.ab-media input[type=file]{max-width:100%;min-width:0}.ab-media progress{width:100%;max-width:100%}' +
				'@media(max-width:600px){.ab-media-row>*{max-width:100%}.ab-media-telemetry{grid-template-columns:1fr 1fr}}'
			]),
			E('h2', {}, [ _('AirBridge.Pi Media') ]),
			E('p', {}, [ _('4K HDMI acceptance is a manual check on the connected display. Decoder telemetry alone does not prove picture, sound, or smooth playback; see the device validation record.') ]),
			E('p', {}, [ _('Only the controller-configured media library is exposed. No filesystem paths, URLs or shell commands can be entered here.') ]),
			E('p', {}, [ _('Playback is fully automatic: each file is decoded with the best engine for its codec (hardware for 4K HEVC, software otherwise) and shown at its native refresh rate on the TV.') ]),
			this.notice, this.engineError, this.capabilityNotice, this.failure, this.title,
			E('div', { 'class': 'ab-media-row' }, [
				this.pause, button(_('Stop'), 'stop', function() { return self.command('stop', []); }),
				button(_('Back 10 seconds'), 'seek', function() { return seek(-10, true); }),
				button(_('Forward 10 seconds'), 'seek', function() { return seek(10, true); }),
				this.seekInput, button(_('Seek'), 'seek', function() {
					return seek(Number(self.seekInput.value), false);
				})
			]),
			E('div', { 'class': 'ab-media-row' }, [
				E('label', {}, [ _('Volume'), ' ', this.volume ]), this.volumeLabel,
				E('label', {}, [ this.mute, ' ', _('Mute') ]),
				E('label', {}, [ _('Audio'), ' ', this.audio ]),
				E('label', {}, [ _('Subtitles'), ' ', this.subtitles ]),
			]),
			this.telemetry,
			E('h3', {}, [ _('Library') ]), this.breadcrumb, this.browseNotice,
			this.storageNotice,
			E('p', {}, [ _('Upload media into the displayed directory without replacing existing files. Files transfer one at a time; Cancel stops the batch and requests cleanup of the current partial file. Completed uploads remain. Playback controls stay available.') ]),
			E('div', { 'class': 'ab-media-row' }, [
				this.filePicker, this.uploadButton, this.cancelUploadButton, this.refreshStorageButton
			]), this.selectionNotice, this.uploadProgress, this.uploadNotice,
			E('div', { 'class': 'ab-media-row' }, [
				this.parent, button(_('Refresh library'), 'browse', function() { return self.browse(self.path, self.offset); }, true)
			]), this.library,
			E('div', { 'class': 'ab-media-row' }, [ this.previous, this.next ]),
			E('h3', {}, [ _('Queue') ]),
			E('p', {}, [ _('Queue controls require media-control permission. Profiles are playback presets, not a claim of 4K decoding support.') ]),
			E('div', { 'class': 'ab-media-row' }, [
				button(_('Refresh queue'), 'queue', function() { return self.refreshQueue(); }),
				button(_('Next item'), 'queue', function() { return self.command('queue', [ 'next', '' ]); }),
				button(_('Clear queue'), 'queue', function() { return self.command('queue', [ 'clear', '' ]); })
			]), this.queue
		]);
		this.updateStatus(initial);
		this.browse('', 0);
		this.refreshQueue();
		this.refreshStorage();
		this.updateFileSelection();
		poll.add(function() { return self.refreshStatus(); }, 2);
		return root;
	},

	fail: function(error) {
		text(this.failure, errorText(error));
	},

	refreshStatus: function() {
		var self = this;
		return api.status().then(checkedStatus).then(function(status) {
			self.updateStatus(status);
		}).catch(function(error) {
			self.updateStatus({ state: 'unavailable', available: false, capabilities: {}, error: errorText(error) });
		});
	},

	updateStatus: function(status) {
		this.status = status;
		var available = status.available === true && status.state !== 'unavailable';
		text(this.notice, available ? _('Engine available') + ' — ' + status.state :
			_('Engine unavailable: playback controls are disabled.'));
		text(this.title, status.title || _('No media playing'));
		text(this.engineError, status.error || '');
		var unsupported = [ 'browse', 'play', 'pause', 'stop', 'seek', 'volume', 'mute', 'tracks', 'queue', 'profile',
			'storage', 'remove', 'upload_begin', 'upload_cancel' ].filter(function(name) {
			return !status.capabilities || status.capabilities[name] !== true;
		});
		text(this.capabilityNotice, unsupported.length ?
			_('Unsupported or currently unavailable controls:') + ' ' + unsupported.join(', ') : _('All media API controls are advertised as available.'));
		text(this.pause, status.state === 'paused' ? _('Resume') : _('Pause'));
		if (document.activeElement !== this.volume)
			this.volume.value = typeof status.volume === 'number' ? status.volume : 0;
		text(this.volumeLabel, typeof status.volume === 'number' ? status.volume + '%' : '—');
		this.mute.checked = status.mute === true;
		this.renderTracks(this.audio, 'audio');
		this.renderTracks(this.subtitles, 'sub');
		var fields = [
			[ _('Position / duration'), clock(status.time_pos) + ' / ' + clock(status.duration) ],
			[ _('Codec'), status.codec ], [ _('Resolution'), status.width && status.height ? status.width + ' × ' + status.height : null ],
			[ _('Hardware decoder (reported)'), status.hwdec ], [ _('Decoder drops'), status.dropped_frames ],
			[ _('Display drops'), status.vo_dropped_frames ],
			[ _('A/V sync (seconds)'), status.avsync ], [ _('Profile'), status.profile ],
			[ _('Media library root'), typeof status.media_root_available === 'boolean' ?
				(status.media_root_available ? _('Available') : _('Unavailable')) : null ],
			[ _('4K HDMI HEVC hardware acceptance'), _('Manual device test; see validation record') ]
		];
		this.telemetry.replaceChildren();
		var self = this;
		fields.forEach(function(pair) {
			self.telemetry.appendChild(E('dt', {}, [ pair[0] ]));
			self.telemetry.appendChild(E('dd', {}, [ pair[1] == null ? '—' : String(pair[1]) ]));
		});
		this.updateControls();
	},

	updateControls: function() {
		var self = this, capabilities = this.status.capabilities || {};
		this.controls.forEach(function(control) {
			var fileOperation = control.capability === 'remove';
			var engineRequired = control.capability !== 'browse' && !fileOperation;
			var isPauseOrStopOrSeek = control.capability === 'pause' || control.capability === 'stop' || control.capability === 'seek';
			control.node.disabled = self.busy || capabilities[control.capability] !== true ||
				(!control.readOnly && !self.writable) ||
				(engineRequired && (self.status.available !== true || self.status.state === 'unavailable' ||
					(isPauseOrStopOrSeek && self.status.state === 'idle')));
			if (fileOperation)
				control.node.disabled = !self.canManageFiles('remove') || self.fileBusy;
		});
		this.previous.disabled = this.previous.disabled || this.offset === 0;
		this.next.disabled = this.next.disabled || !this.more || this.scanLimited ||
			this.nextOffset <= this.offset || this.nextOffset > 4096;
		this.parent.disabled = this.parent.disabled || this.path === '';
		[ this.audio, this.subtitles ].forEach(function(select) {
			select.disabled = select.disabled || select.options.length === 0;
		});
		this.updateFileControls();
	},

	renderTracks: function(select, type) {
		if (document.activeElement === select)
			return;
		select.replaceChildren();
		var tracks = (Array.isArray(this.status.track_list) ? this.status.track_list : []).filter(function(track) {
			return track.type === type && Number.isInteger(track.id) && track.id >= 1 && track.id <= 65535;
		});
		if (tracks.length) {
			var off = E('option', { value: 0 }, [ _('Off') ]);
			off.selected = !tracks.some(function(track) { return track.selected === true; });
			select.appendChild(off);
		}
		tracks.forEach(function(track) {
			var option = E('option', { value: track.id }, [
				String(track.id) + ' — ' + (track.title || track.lang || type)
			]);
			option.selected = track.selected === true;
			select.appendChild(option);
		});
	},

	command: function(method, args) {
		var self = this, capabilities = this.status.capabilities || {};
		if (this.busy || !this.writable || !this.status.capabilities ||
			this.status.available !== true || this.status.state === 'unavailable')
			return Promise.resolve();
		if (method === 'volume' ?
			(args[0] !== this.status.volume && capabilities.volume !== true ||
			 args[1] !== this.status.mute && capabilities.mute !== true) :
			capabilities[method] !== true)
			return Promise.resolve();
		this.busy = true;
		text(this.failure, '');
		this.updateControls();
		return api[method].apply(null, args).then(checked).then(function() {
			return Promise.all([ self.refreshStatus(), self.refreshQueue() ]);
		}).catch(function(error) { self.fail(error); }).finally(function() {
			self.busy = false;
			self.updateControls();
		});
	},

	browse: function(path, offset) {
		var self = this;
		if (!relativePath(path, true) || !Number.isInteger(offset) || offset < 0 || offset > 4096) {
			this.fail(_('Invalid library location.'));
			return Promise.resolve();
		}
		var serial = ++this.browseSerial;
		return api.browse(path, offset, this.limit).then(checked).then(function(result) {
			if (serial !== self.browseSerial)
				return;
			if (!relativePath(result.path, true) || !Array.isArray(result.entries) ||
				!Number.isInteger(result.offset) || result.offset < 0 || typeof result.more !== 'boolean')
				throw new Error(_('Invalid library response'));
			self.path = result.path;
			self.offset = result.offset;
			self.nextOffset = result.offset + result.entries.length;
			if (!Object.prototype.hasOwnProperty.call(self.pageHistory, self.path))
				Object.defineProperty(self.pageHistory, self.path, { value: [], enumerable: true });
			if (self.pageHistory[self.path].indexOf(self.offset) === -1)
				self.pageHistory[self.path].push(self.offset);
			self.pageHistory[self.path].sort(function(a, b) { return a - b; });
			self.more = result.more;
			self.scanLimited = result.scan_limited === true;
			text(self.browseNotice, self.scanLimited ?
				_('Directory scan limit reached. Open a subdirectory to narrow the library; further pagination is disabled.') : '');
			text(self.breadcrumb, _('Library') + ' /' + self.path + ' — ' + _('Offset') + ' ' + self.offset);
			self.library.replaceChildren();
			/* Discard controls for old rows so pagination cannot grow memory. */
			self.controls = self.controls.filter(function(control) { return !control.library; });
			result.entries.forEach(function(entry) {
				if (!relativePath(entry.path, false) || typeof entry.name !== 'string' ||
					[ 'file', 'directory' ].indexOf(entry.type) === -1)
					return;
				function action(label, capability, handler, readOnly) {
					var node = E('button', { 'class': 'cbi-button', 'type': 'button', 'click': handler }, [ label ]);
					self.controls.push({ node: node, capability: capability, readOnly: !!readOnly, library: true });
					return node;
				}
				var actions = entry.type === 'directory' ?
					[ action(_('Open'), 'browse', function() { return self.browse(entry.path, 0); }, true) ] : [
						action(_('Play'), 'play', function() { return self.command('play', [ entry.path ]); }),
						action(_('Add to queue'), 'queue', function() { return self.command('queue', [ 'append', entry.path ]); }),
						action(_('Delete'), 'remove', function() { return self.removeFile(entry); })
					];
				self.library.appendChild(E('div', { 'class': 'ab-media-entry' }, [
					E('span', { 'class': 'ab-media-name' }, [ entry.name ]),
					E('span', {}, [ entry.type === 'file' ? bytes(entry.size_bytes) : _('Directory') ])
				].concat(actions)));
			});
			if (result.entries.length === 0)
				self.library.appendChild(E('p', {}, [ _('No media entries in this directory.') ]));
			self.updateControls();
			self.updateFileSelection();
		}).catch(function(error) { if (serial === self.browseSerial) self.fail(error); });
	},

	canManageFiles: function(operation) {
		var storage = this.storageInfo, capabilities = this.status.capabilities || {};
		if (!this.writable || !storage || storage.mounted !== true || storage.writable !== true)
			return false;
		return operation === 'remove' ? storage['delete'] === true && capabilities.remove === true :
			storage.upload === true && capabilities.upload_begin === true && capabilities.upload_cancel === true &&
			storage.max_file_bytes > 0 && storage.active_uploads < storage.max_uploads;
	},

	updateFileControls: function() {
		var allowed = !!this.canManageFiles();
		this.filePicker.disabled = !allowed || this.fileBusy;
		this.uploadButton.disabled = !allowed || this.fileBusy || this.selectedFiles.length === 0;
		this.cancelUploadButton.disabled = !this.transfer || this.transfer.cancelled;
		this.refreshStorageButton.disabled = this.fileBusy;
	},

	updateFileSelection: function() {
		var total = this.selectedFiles.reduce(function(sum, file) { return sum + file.size; }, 0);
		text(this.selectionNotice, this.selectedFiles.length ?
			_('Selected for') + ' /' + this.path + ': ' + this.selectedFiles.map(function(file) {
				return file.name + ' (' + bytes(file.size) + ')';
			}).join(', ') + ' — ' + _('Total') + ': ' + bytes(total) : _('No files selected.'));
		this.updateFileControls();
	},

	refreshStorage: function() {
		var self = this;
		return api.storage().then(checked).then(function(storage) {
			if ([ 'mounted', 'writable', 'upload', 'delete' ].some(function(field) { return typeof storage[field] !== 'boolean'; }) ||
				[ 'total_bytes', 'free_bytes', 'usable_bytes', 'reserve_bytes', 'max_file_bytes', 'chunk_size',
					'active_uploads', 'max_uploads' ].some(function(field) {
						return !Number.isSafeInteger(storage[field]) || storage[field] < 0;
					}) || storage.chunk_size < 1 || storage.chunk_size > 1048576)
				throw new Error(_('Invalid media storage response'));
			self.storageInfo = storage;
			text(self.storageNotice, (storage.mounted ? _('Media storage mounted') : _('Media storage unavailable')) +
				' — ' + _('Free') + ': ' + bytes(storage.free_bytes) + ' / ' + bytes(storage.total_bytes) + '; ' +
				_('Usable') + ': ' + bytes(storage.usable_bytes) + '; ' +
				_('Safety reserve') + ': ' + bytes(storage.reserve_bytes) + '; ' +
				_('Maximum file now') + ': ' + bytes(storage.max_file_bytes) + '; ' +
				_('Active uploads') + ': ' + storage.active_uploads + '/' + storage.max_uploads +
				(!self.writable ? ' — ' + _('Read-only LuCI session') :
					!storage.writable ? ' — ' + _('Upload and delete are unavailable') : '') +
				(storage.reason ? ' — ' + String(storage.reason) : ''));
			self.updateControls();
			return storage;
		}).catch(function(error) {
			self.storageInfo = null;
			text(self.storageNotice, _('Media storage unavailable:') + ' ' + errorText(error));
			self.updateControls();
			return null;
		});
	},

	removeFile: async function(entry) {
		if (!this.canManageFiles('remove') || this.fileBusy || entry.type !== 'file' ||
			!relativePath(entry.path, false))
			return false;
		if (!window.confirm(_('Permanently delete this media file? This cannot be undone.') +
			'\n\n' + entry.name + ' (' + bytes(entry.size_bytes) + ')\n\n' +
			_('A file held for playback cannot be deleted. Press Stop first.')))
			return false;
		this.fileBusy = true;
		text(this.failure, '');
		this.updateControls();
		try {
			var result = checked(await api.remove(entry.path));
			if (result.path !== entry.path)
				throw new Error(_('File deletion was not confirmed. Refresh the library before retrying.'));
			return true;
		} catch (error) {
			this.fail(error);
			return false;
		} finally {
			await Promise.all([ this.browse(this.path, this.offset), this.refreshQueue(), this.refreshStorage() ]);
			this.fileBusy = false;
			this.updateControls();
		}
	},

	uploadSelected: async function() {
		if (!this.canManageFiles() || this.fileBusy || this.selectedFiles.length === 0)
			return false;
		var files = this.selectedFiles.slice(), target = this.path, total = 0, completed = 0;
		for (var i = 0; i < files.length; i++) {
			if (!uploadName(files[i].name) || !Number.isSafeInteger(files[i].size) || files[i].size <= 0 ||
				!relativePath((target ? target + '/' : '') + files[i].name, false)) {
				this.fail(_('Select nonempty supported media files with safe filenames.'));
				return false;
			}
			total += files[i].size;
		}
		if (!Number.isSafeInteger(total)) {
			this.fail(_('Selected files are too large.'));
			return false;
		}
		var transfer = { cancelled: false, xhr: null, token: null, completedBytes: 0, total: total };
		this.transfer = transfer;
		this.fileBusy = true;
		this.uploadProgress.value = 0;
		text(this.failure, '');
		this.updateControls();
		try {
			for (var index = 0; index < files.length; index++) {
				if (transfer.cancelled)
					throw new Error(_('Upload canceled.'));
				var storage = await this.refreshStorage(), file = files[index];
				if (transfer.cancelled)
					throw new Error(_('Upload canceled.'));
				if (!storage || !this.canManageFiles() || file.size > storage.max_file_bytes ||
					total - transfer.completedBytes > Math.min(storage.usable_bytes, storage.max_file_bytes))
					throw new Error(_('Not enough writable media space after the safety reserve.'));
				transfer.file = file;
				transfer.index = index;
				transfer.count = files.length;
				transfer.offset = 0;
				transfer.path = (target ? target + '/' : '') + file.name;
				this.showUploadProgress(transfer, 0);
				var begin = checked(await api.upload_begin(transfer.path, file.size));
				if (typeof begin.token !== 'string' || !/^[a-f0-9]{64}$/.test(begin.token))
					throw new Error(_('Invalid upload capability from media controller.'));
				transfer.token = begin.token;
				if (transfer.cancelled)
					throw new Error(_('Upload canceled.'));
				var chunkSize = begin.chunk_size;
				if (!Number.isSafeInteger(chunkSize) || chunkSize <= 0 || chunkSize > 1048576 ||
					begin.offset !== 0 || begin.size !== file.size || begin.path !== transfer.path)
					throw new Error(_('Invalid upload chunk size.'));
				while (transfer.offset < file.size) {
					var end = Math.min(file.size, transfer.offset + chunkSize);
					var reply = await this.postUpload(transfer, transfer.offset, end);
					if (reply.offset !== end || reply.size !== file.size || reply.path !== transfer.path ||
						reply.complete !== (end === file.size))
						throw new Error(_('Unexpected upload offset. Transfer stopped without retrying.'));
					transfer.offset = end;
					if (reply.complete)
						transfer.token = null;
					this.showUploadProgress(transfer, 0);
				}
				completed++;
				transfer.completedBytes += file.size;
			}
			text(this.uploadNotice, _('Upload complete. Files added:') + ' ' + completed);
			this.uploadProgress.value = 100;
			return true;
		} catch (error) {
			text(this.uploadNotice, transfer.cancelled ?
				_('Upload canceled. Completed files may remain; remaining files were not sent.') :
				_('Upload stopped. Completed files remain; refresh before retrying.'));
			if (!transfer.cancelled)
				this.fail(error);
			return false;
		} finally {
			if (transfer.token) {
				try {
					checked(await api.upload_cancel(transfer.token));
				} catch (error) {
					text(this.failure, this.failure.textContent + ' ' +
						_('Partial upload cleanup was not confirmed. Refresh storage and let the controller expire the upload.'));
				}
			}
			transfer.token = null;
			transfer.xhr = null;
			this.transfer = null;
			this.selectedFiles = [];
			this.filePicker.value = '';
			await Promise.all([ this.refreshStorage(), this.browse(this.path, this.offset) ]);
			this.fileBusy = false;
			this.updateFileSelection();
			this.updateControls();
		}
	},

	showUploadProgress: function(transfer, inFlight) {
		var loaded = Math.min(transfer.file.size, transfer.offset + inFlight);
		this.uploadProgress.value = (transfer.completedBytes + loaded) / transfer.total * 100;
		text(this.uploadNotice, _('Uploading') + ' ' + (transfer.index + 1) + '/' + transfer.count +
			': ' + transfer.file.name + ' — ' + bytes(loaded) + ' / ' + bytes(transfer.file.size));
	},

	postUpload: async function(transfer, start, end) {
		if (transfer.cancelled)
			throw new Error(_('Upload canceled.'));
		if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || end <= start ||
			end > transfer.file.size || end - start > 1048576)
			throw new Error(_('Invalid upload byte range.'));
		var session = L.env && L.env.sessionid;
		if (typeof session !== 'string' || !/^[a-f0-9]{32}$/.test(session))
			throw new Error(_('Authenticated LuCI session is unavailable. Sign in again.'));
		var base = L.env.cgi_base;
		if (typeof base !== 'string' || !/^\/(?:[A-Za-z0-9_-]+\/)*[A-Za-z0-9_-]+$/.test(base))
			throw new Error(_('Same-origin upload endpoint is unavailable.'));
		var self = this;
		try {
			var response = await request.post(base + '/airbridge-media-upload', function(xhr) {
					if (transfer.cancelled)
						throw new Error(_('Upload canceled.'));
					transfer.xhr = xhr;
					// A content callback is required: LuCI JSON-stringifies a direct Blob.
					return transfer.file.slice(start, end);
				}, {
					headers: { 'Content-Type': 'application/octet-stream', 'Authorization': 'Bearer ' + session + ':' + transfer.token + ':' + start },
					// LuCI appends a timestamp query even to POST unless cache is true.
					// This preserves the URL; CGI still sends Cache-Control: no-store.
					cache: true,
					timeout: 45000,
					progress: function(event) {
						if (!transfer.cancelled)
							self.showUploadProgress(transfer, Math.max(0, Math.min(end - start, event.loaded || 0)));
					}
				});
			if (transfer.cancelled)
				throw new Error(_('Upload canceled.'));
			if (!response || !Number.isInteger(response.status) || typeof response.json !== 'function')
				throw new Error(_('Upload request failed. No automatic retry was attempted.'));
			if (response.status === 401 || response.status === 403)
				throw new Error(_('Upload authentication or permission failed. Sign in again and refresh storage.'));
			var payload = checked(await response.json());
			if (response.status < 200 || response.status >= 300)
				throw new Error(_('Upload request failed. No automatic retry was attempted.'));
			return payload;
		} catch (error) {
			var message = errorText(error);
			if (message.indexOf(session) !== -1 || transfer.token && message.indexOf(transfer.token) !== -1)
				throw new Error(_('Upload transport failed. Refresh the library before retrying.'));
			throw error;
		} finally {
			transfer.xhr = null;
		}
	},

	cancelUpload: function() {
		if (!this.transfer || this.transfer.cancelled)
			return;
		this.transfer.cancelled = true;
		text(this.uploadNotice, _('Canceling upload and cleaning up the current partial file…'));
		if (this.transfer.xhr)
			this.transfer.xhr.abort();
		this.updateFileControls();
	},

	refreshQueue: function() {
		var self = this;
		if (!this.writable || !this.status.capabilities || this.status.capabilities.queue !== true) {
			text(this.queue, _('Queue unavailable or media-control permission is required.'));
			return Promise.resolve();
		}
		return api.queue('list', '').then(checked).then(function(result) {
			if (!Array.isArray(result.entries))
				throw new Error(_('Invalid queue response'));
			self.queue.replaceChildren();
			result.entries.forEach(function(entry) {
				if (!relativePath(entry, false))
					return;
				self.queue.appendChild(E('li', {}, [
					entry
				]));
			});
			if (result.entries.length === 0)
				self.queue.appendChild(E('li', {}, [ _('Queue is empty.') ]));
		}).catch(function(error) { self.fail(error); });
	}
});
