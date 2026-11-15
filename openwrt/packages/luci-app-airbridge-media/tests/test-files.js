'use strict';

// No router, browser, network, third-party modules, or actual credentials.
const assert = require('node:assert/strict');
const { harness, state, settle, descendants } = require('./test-ui.js');
const CHUNK = 1048576;
const sid = '0'.repeat(32); // Synthetic session for header-scope assertions only.
const ok = extra => Object.assign({ ok: true, error: null }, extra);
const http = (extra, status = 200) => ({ status, json: () => ok(extra) });
const storage = overrides => ok(Object.assign({
	mounted: true, writable: true, upload: true, delete: true, reason: null,
	total_bytes: 16 * 1024 ** 3, free_bytes: 10 * 1024 ** 3, usable_bytes: 9.75 * 1024 ** 3,
	reserve_bytes: 256 * 1024 ** 2, max_file_bytes: 9.75 * 1024 ** 3,
	chunk_size: CHUNK, active_uploads: 0, max_uploads: 2
}, overrides));

function file(name, data) {
	const blob = new Blob([data]);
	Object.defineProperty(blob, 'name', { value: name });
	return blob;
}

async function ready(options) {
	const h = harness(options);
	h.tree = h.view.render(await h.view.load());
	await settle();
	return h;
}

function choose(h, files) {
	h.view.filePicker.files = files;
	h.view.filePicker.listeners.change();
}

function server() {
	const files = [], chunks = [];
	let current, serial = 0, active = 0, maximumActive = 0;
	return {
		files, chunks,
		get maximumActive() { return maximumActive; },
		begin: (path, size) => {
			assert(!current, 'begin only after prior file publication');
			current = { path, size, received: 0, token: (++serial).toString(16).repeat(64), pieces: [] };
			return ok({ token: current.token, chunk_size: CHUNK, offset: 0, size, path, expires_in: 900, max_size: 9 * 1024 ** 3 });
		},
		transport: async record => {
			active++;
			maximumActive = Math.max(maximumActive, active);
			try {
				assert.equal(typeof record.data, 'function', 'LuCI must receive a callback, not a directly JSON-stringified Blob');
				assert.equal(record.url, '/cgi-bin/airbridge-media-upload');
				assert.equal(record.originalUrl, record.url, 'wire URL must remain query-free after LuCI processing');
				assert.equal(record.options.cache, true, 'suppress LuCI timestamp query even on POST');
				assert(!Object.prototype.hasOwnProperty.call(record.options, 'query'));
				assert(!record.url.includes(sid));
				assert(!record.url.includes(current.token));
				assert.equal(record.options.headers.Authorization, 'Bearer ' + sid + ':' + current.token + ':' + current.received);
				assert.equal(record.options.headers['Content-Type'], 'application/octet-stream');
				assert.deepEqual(Object.keys(record.options.headers).sort(), ['Authorization', 'Content-Type']);
				assert.equal(record.options.timeout, 45000);
				const offset = Number(record.options.headers.Authorization.split(':')[2]);
				const body = record.data({ abort() { throw new Error('unexpected abort'); } });
				assert(body instanceof Blob, 'callback returns a raw Blob');
				assert(body.size >= 1 && body.size <= CHUNK);
				assert.equal(offset, current.received);
				chunks.push({ offset, size: body.size });
				record.options.progress({ loaded: body.size, total: body.size });
				current.pieces.push(Buffer.from(await body.arrayBuffer()));
				current.received += body.size;
				const reply = { offset: current.received, size: current.size, path: current.path,
					complete: current.received === current.size };
				if (reply.complete) {
					files.push({ path: current.path, data: Buffer.concat(current.pieces) });
					current = null;
				}
				return http(reply);
			} finally { active--; }
		}
	};
}

async function run() {
	const s = server(), h = await ready({ transport: record => s.transport(record) });
	h.responses.upload_begin = s.begin;
	const firstBytes = Buffer.alloc(CHUNK * 2 + 7, 91), secondBytes = Buffer.from([0, 255, 3]);
	const first = file('<img onerror=bad>.mkv', firstBytes), second = file('second.mp4', secondBytes);
	choose(h, [first, second]);
	assert.match(h.view.selectionNotice.textContent, /<img onerror=bad>.mkv/);
	assert.equal(descendants(h.tree, node => node.tag === 'img').length, 0);
	assert.match(h.view.storageNotice.textContent, /10.00 GiB/);
	assert.match(h.view.library.textContent, /2.00 KiB/);
	assert.equal(h.view.uploadButton.disabled, false);
	assert.equal(await h.view.uploadSelected(), true);
	assert.equal(s.maximumActive, 1, 'never upload concurrent chunks/files');
	assert.deepEqual(s.chunks, [
		{ offset: 0, size: CHUNK },
		{ offset: CHUNK, size: CHUNK },
		{ offset: CHUNK * 2, size: 7 },
		{ offset: 0, size: 3 }
	]);
	assert.equal(s.files[0].path, first.name);
	assert(s.files[0].data.equals(firstBytes));
	assert(s.files[1].data.equals(secondBytes));
	assert.equal(h.view.uploadProgress.value, 100);
	assert.equal(h.view.fileBusy, false);
	assert.equal(h.view.transfer, null);
	assert(!h.calls.some(call => call.method === 'upload_cancel'), 'published uploads need no cancellation');
	assert(!h.tree.textContent.includes(sid), 'session never appears in DOM');
	assert(!JSON.stringify(h.tree).includes(sid), 'session never appears in DOM attributes');
	assert(!h.calls.some(call => call.method === 'stop'), 'upload must not stop playback');

	const deletion = await ready();
	const entry = { type: 'file', name: '<script>file</script>.mkv', path: 'movie.mkv', size_bytes: 2048 };
	assert.equal(await deletion.view.removeFile(entry), false);
	assert.equal(deletion.confirmations.length, 1);
	assert.match(deletion.confirmations[0], /Permanently delete/);
	assert.match(deletion.confirmations[0], /cannot be undone/);
	assert.match(deletion.confirmations[0], /<script>file<\/script>/);
	assert(!deletion.calls.some(call => call.method === 'remove'));
	deletion.setConfirmed(true);
	assert.equal(await deletion.view.removeFile({ type: 'directory', name: 'folder', path: 'folder' }), false);
	assert.equal(deletion.confirmations.length, 1, 'never offer recursive directory deletion');
	assert.equal(await deletion.view.removeFile(entry), true);
	assert(deletion.calls.some(call => call.method === 'remove' && call.args[0] === 'movie.mkv'));
	assert(!deletion.calls.some(call => call.method === 'stop'), 'delete must not silently stop active media');
	deletion.responses.remove = () => ({ ok: false, error: '<b>Stop playback first</b>' });
	assert.equal(await deletion.view.removeFile(entry), false);
	assert.equal(deletion.view.failure.textContent, '<b>Stop playback first</b>');

	const ro = await ready({ writable: false });
	choose(ro, [file('read-only.mkv', 'data')]);
	ro.setConfirmed(true);
	assert.equal(ro.view.filePicker.disabled, true);
	assert.equal(ro.view.uploadButton.disabled, true);
	assert.equal(await ro.view.uploadSelected(), false);
	assert.equal(await ro.view.removeFile(entry), false);
	assert(!ro.calls.some(call => ['remove', 'upload_begin', 'upload_cancel'].includes(call.method)));
	assert.match(ro.view.storageNotice.textContent, /Read-only/);

	const absent = await ready();
	absent.responses.storage = () => storage({ mounted: false, writable: false, upload: false, delete: false,
		free_bytes: 0, usable_bytes: 0, max_file_bytes: 0, reason: '<b>Root filesystem refused</b>' });
	await absent.view.refreshStorage();
	choose(absent, [file('not-root.mkv', 'data')]);
	assert.equal(await absent.view.uploadSelected(), false);
	assert.equal(absent.view.uploadButton.disabled, true);
	assert.match(absent.view.storageNotice.textContent, /<b>Root filesystem refused<\/b>/);
	assert(!absent.calls.some(call => call.method === 'upload_begin'));
	const full = await ready();
	full.responses.storage = () => storage({ free_bytes: 1100, usable_bytes: 76, reserve_bytes: 1024, max_file_bytes: 76 });
	choose(full, [file('too-big.mkv', Buffer.alloc(77))]);
	assert.equal(await full.view.uploadSelected(), false);
	assert.match(full.view.failure.textContent, /Not enough writable media space/);
	assert(!full.calls.some(call => call.method === 'upload_begin'));

	const duplicate = await ready();
	duplicate.responses.upload_begin = () => ({ ok: false, error: '<img>Target already exists' });
	choose(duplicate, [file('exists.mkv', 'data'), file('not-sent.mkv', 'data')]);
	assert.equal(await duplicate.view.uploadSelected(), false);
	assert.match(duplicate.view.failure.textContent, /<img>Target already exists/);
	assert.equal(duplicate.calls.filter(call => call.method === 'upload_begin').length, 1);
	assert.equal(duplicate.requests.length, 0);
	assert.equal(duplicate.view.fileBusy, false);

	let inflight, aborts = 0;
	const canceled = await ready({ transport: record => new Promise((resolve, reject) => {
		const body = record.data({ abort() { aborts++; reject(new Error('test abort')); } });
		assert(body instanceof Blob);
		inflight = { resolve, record };
	}) });
	choose(canceled, [file('cancel.mkv', Buffer.alloc(CHUNK + 1)), file('not-sent.mkv', 'data')]);
	const pending = canceled.view.uploadSelected();
	await settle();
	assert(inflight);
	assert.equal(canceled.view.pause.disabled, false, 'playback controls remain enabled during upload');
	await canceled.view.pause.trigger('click');
	assert(canceled.calls.some(call => call.method === 'pause'));
	assert.equal(canceled.view.cancelUploadButton.disabled, false);
	canceled.view.cancelUpload();
	assert.equal(await pending, false);
	assert.equal(aborts, 1);
	assert.equal(canceled.calls.filter(call => call.method === 'upload_cancel').length, 1);
	assert.equal(canceled.calls.filter(call => call.method === 'upload_begin').length, 1);
	assert.equal(canceled.view.transfer, null);
	assert.equal(canceled.view.fileBusy, false);
	assert.match(canceled.view.uploadNotice.textContent, /canceled/);

	const beforeBegin = await ready();
	let finishBegin;
	beforeBegin.responses.upload_begin = () => new Promise(resolve => { finishBegin = resolve; });
	choose(beforeBegin, [file('begin.mkv', 'data')]);
	const beginPending = beforeBegin.view.uploadSelected();
	await settle();
	beforeBegin.view.cancelUpload();
	finishBegin(ok({ token: 'b'.repeat(64), chunk_size: CHUNK, offset: 0, size: 4, path: 'begin.mkv' }));
	assert.equal(await beginPending, false);
	assert.equal(beforeBegin.requests.length, 0);
	assert.equal(beforeBegin.calls.filter(call => call.method === 'upload_cancel').length, 1);

	const wrong = await ready({ transport: record => {
		record.data({ abort() {} });
		return Promise.resolve(http({ offset: 999, size: 4, path: 'wrong-offset.mkv', complete: false }));
	} });
	choose(wrong, [file('wrong-offset.mkv', 'data')]);
	assert.equal(await wrong.view.uploadSelected(), false);
	assert.match(wrong.view.failure.textContent, /Unexpected upload offset/);
	assert.equal(wrong.calls.filter(call => call.method === 'upload_cancel').length, 1);
	assert.equal(wrong.requests.length, 1, 'do not retry or finalize after an invalid offset');

	const auth = await ready({ transport: record => {
		record.data({ abort() {} });
		return Promise.resolve(http({}, 403));
	} });
	choose(auth, [file('auth.mkv', 'data')]);
	assert.equal(await auth.view.uploadSelected(), false);
	assert.match(auth.view.failure.textContent, /authentication or permission/);
	assert.equal(auth.calls.filter(call => call.method === 'upload_cancel').length, 1);
	assert(!auth.tree.textContent.includes(sid));

	const malformed = await ready();
	for (const name of ['../x.mkv', '/absolute.mkv', '.hidden.mkv', 'scheme:x.mkv', 'x\\y.mkv', 'x.html']) {
		choose(malformed, [file(name, 'data')]);
		assert.equal(await malformed.view.uploadSelected(), false);
	}
	assert(!malformed.calls.some(call => call.method === 'upload_begin'));
	assert.equal(malformed.requests.length, 0);

	const offlineServer = server();
	const offline = await ready({ status: state({ available: false, state: 'unavailable', error: 'Player offline' }),
		transport: record => offlineServer.transport(record) });
	offline.responses.upload_begin = offlineServer.begin;
	choose(offline, [file('offline.mkv', 'data')]);
	assert.equal(offline.view.pause.disabled, true);
	assert.equal(offline.view.uploadButton.disabled, false, 'engine availability must not gate file operations');
	assert.equal(await offline.view.uploadSelected(), true);
	offline.setConfirmed(true);
	assert.equal(await offline.view.removeFile(entry), true);

	const denied = await ready({ status: state({ capabilities: { browse: true, storage: true } }) });
	choose(denied, [file('unsupported.mkv', 'data')]);
	denied.setConfirmed(true);
	assert.equal(await denied.view.uploadSelected(), false);
	assert.equal(await denied.view.removeFile(entry), false);
	assert.equal(denied.view.filePicker.disabled, true);
	const slots = await ready();
	slots.responses.storage = () => storage({ active_uploads: 2, max_uploads: 2 });
	await slots.view.refreshStorage();
	choose(slots, [file('busy.mkv', 'data')]);
	assert.equal(slots.view.uploadButton.disabled, true);
	slots.setConfirmed(true);
	assert.equal(await slots.view.removeFile(entry), true, 'full upload slots must not disable delete');

	const lost = await ready({ transport: record => {
		const body = record.data({ abort() {} });
		assert.equal(body.size, 4);
		// Simulate a final publication whose response was lost; never retry.
		return Promise.reject(new Error('Response lost after commit'));
	} });
	lost.responses.upload_cancel = () => ok({ canceled: false });
	choose(lost, [file('maybe-published.mkv', 'data')]);
	const browseBefore = lost.calls.filter(call => call.method === 'browse').length;
	assert.equal(await lost.view.uploadSelected(), false);
	assert.equal(lost.requests.length, 1);
	assert.equal(lost.calls.filter(call => call.method === 'upload_cancel').length, 1);
	assert(lost.calls.filter(call => call.method === 'browse').length > browseBefore);
	assert.match(lost.view.uploadNotice.textContent, /refresh before retrying/);
	assert(!lost.view.failure.textContent.includes('cleanup was not confirmed'), 'idempotent canceled:false is success');

	const echo = await ready({ transport: record => {
		record.data({ abort() {} });
		return Promise.resolve({ status: 409, json: () => ({ ok: false, error: record.options.headers.Authorization }) });
	} });
	choose(echo, [file('echo.mkv', 'data')]);
	assert.equal(await echo.view.uploadSelected(), false);
	assert.match(echo.view.failure.textContent, /Upload transport failed/);
	assert(!echo.tree.textContent.includes(sid));
	assert(!echo.tree.textContent.includes('a'.repeat(64)));
	assert(!JSON.stringify(echo.tree).includes(sid));

	const cleanup = await ready({ transport: record => {
		record.data({ abort() {} });
		return Promise.reject(new Error('Original upload error'));
	} });
	cleanup.responses.upload_cancel = () => ({ ok: false, error: 'cleanup unavailable' });
	choose(cleanup, [file('cleanup.mkv', 'data')]);
	assert.equal(await cleanup.view.uploadSelected(), false);
	assert.match(cleanup.view.failure.textContent, /Original upload error/);
	assert.match(cleanup.view.failure.textContent, /cleanup was not confirmed/);

	for (const field of ['sessionid', 'cgi_base']) {
		const endpoint = await ready();
		endpoint.env[field] = field === 'sessionid' ? undefined : 'https://different-origin.invalid/cgi-bin';
		choose(endpoint, [file('endpoint.mkv', 'data')]);
		assert.equal(await endpoint.view.uploadSelected(), false);
		assert.equal(endpoint.requests.length, 0, 'never send credentials to an unvalidated endpoint');
		assert.equal(endpoint.calls.filter(call => call.method === 'upload_cancel').length, 1);
	}
	const invalidStorage = await ready();
	invalidStorage.responses.storage = () => storage({ free_bytes: 'untrusted' });
	await invalidStorage.view.refreshStorage();
	assert.equal(invalidStorage.view.storageInfo, null);
	assert.equal(invalidStorage.view.filePicker.disabled, true);
	assert.match(invalidStorage.view.storageNotice.textContent, /Invalid media storage/);

	const paging = await ready({ browse: (location, start) => ok({ path: location, offset: start, more: start < 5,
		entries: Array.from({ length: start === 0 ? 2 : 3 }, (_, n) => ({
			name: 'item' + n + '.mkv', path: 'item' + n + '.mkv', type: 'file', size_bytes: n
		})) }) });
	await paging.view.next.trigger('click');
	assert.equal(paging.view.offset, 2, 'short pages use returned entry count, not requested limit');
	await paging.view.next.trigger('click');
	assert.equal(paging.view.offset, 5);
	await paging.view.previous.trigger('click');
	assert.equal(paging.view.offset, 2, 'previous returns an observed page boundary');

	const offset = 2 ** 32 + 7;
	const large = await ready({ transport: record => {
		const body = record.data({ abort() {} });
		assert.equal(body.size, 3);
		assert.equal(record.url, '/cgi-bin/airbridge-media-upload');
		assert.equal(record.options.headers.Authorization, 'Bearer ' + sid + ':' + 'c'.repeat(64) + ':' + offset);
		return Promise.resolve(http({ offset: offset + 3, size: offset + 3, path: 'large.mkv', complete: true }));
	} });
	const virtual = { name: 'large.mkv', size: offset + 3, slice(start, end) {
		assert.equal(start, offset);
		assert.equal(end, offset + 3);
		return new Blob([new Uint8Array(end - start)]);
	} };
	await large.view.postUpload({ file: virtual, token: 'c'.repeat(64), offset, completedBytes: 0,
		total: virtual.size, index: 0, count: 1 }, offset, offset + 3);
	choose(large, [virtual]);
	assert.match(large.view.selectionNotice.textContent, /4.00 GiB/);

	console.log('PASS: raw callback Blob bytes/chunk offsets, sequential files, header-only auth, >4GiB offsets, free space/size, safe deletion confirmation, read-only, duplicates, abort and cleanup');
	console.log('No router/browser upload or playback performance validation performed.');
}

run().catch(error => { console.error(error); process.exitCode = 1; });
