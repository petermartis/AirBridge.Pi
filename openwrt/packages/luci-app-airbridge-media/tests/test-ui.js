'use strict';

// Run on a development host: node openwrt/packages/luci-app-airbridge-media/tests/test-ui.js
// A small text-only DOM model, not browser or HDMI hardware validation.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'htdocs/luci-static/resources/view/airbridge/media.js'), 'utf8');
const readJSON = name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
const methods = {
	status: [], browse: ['path', 'offset', 'limit'], play: ['path'],
	pause: ['paused'], stop: [], seek: ['seconds', 'relative'],
	volume: ['value', 'mute'], tracks: ['type', 'id'],
	queue: ['action', 'path'], profile: ['name'],
	storage: [], remove: ['path'], upload_begin: ['path', 'size'], upload_cancel: ['token']
};

class TextNode {
	constructor(value) { this.textContent = String(value); }
}

class Element {
	constructor(tag, attrs = {}, children = []) {
		this.tag = tag;
		this.attrs = attrs;
		this.listeners = {};
		this.children = [];
		this.disabled = false;
		this.checked = false;
		this.value = attrs.value == null ? '' : String(attrs.value);
		for (const [name, value] of Object.entries(attrs))
			if (typeof value === 'function') this.listeners[name] = value;
		for (const child of children) this.appendChild(child);
	}
	appendChild(child) {
		this.children.push(child instanceof Element || child instanceof TextNode ? child : new TextNode(child));
		return child;
	}
	replaceChildren(...children) {
		this.children = [];
		for (const child of children) this.appendChild(child);
	}
	get textContent() { return this.children.map(child => child.textContent).join(''); }
	set textContent(value) { this.children = [new TextNode(value)]; }
	get options() { return this.children.filter(child => child.tag === 'option'); }
	addEventListener(name, handler) { this.listeners[name] = handler; }
	set innerHTML(_) { throw new Error('HTML injection API used'); }
	async trigger(name) {
		assert.equal(this.disabled, false, 'attempt to interact with a disabled control');
		return this.listeners[name]();
	}
}

function descendants(node, predicate) {
	if (!(node instanceof Element)) return [];
	return (predicate(node) ? [node] : []).concat(node.children.flatMap(child => descendants(child, predicate)));
}

function state(overrides = {}) {
	return Object.assign({
		ok: true, error: null, available: true, state: 'playing',
		title: '<img src=x onerror=alert(1)>', time_pos: 65, duration: 3600,
		volume: 40, mute: false, profile: 'balanced', codec: 'hevc',
		width: 3840, height: 2160, hwdec: 'drm', dropped_frames: 0, avsync: 0.001,
		track_list: [
			{ id: 1, type: 'audio', title: '<script>evil()</script>', selected: true },
			{ id: 2, type: 'sub', lang: 'eng', selected: false }
		],
		capabilities: Object.fromEntries(['browse', 'play', 'pause', 'stop', 'seek', 'volume', 'mute', 'tracks',
			'queue', 'profile', 'storage', 'remove', 'upload_begin', 'upload_cancel'].map(name => [name, true]))
	}, overrides);
}

function harness({ writable = true, status = state(), browse, transport } = {}) {
	const calls = [], declarations = {}, polling = [], document = { activeElement: null }, confirmations = [], requests = [];
	let confirmed = false;
	const responses = {
		status: () => status,
		browse: browse || ((location, offset) => ({
			ok: true, error: null, path: location, offset, more: offset === 0,
			entries: [
				{ name: '<img src=x onerror=alert(1)>.mkv', path: 'movie.mkv', type: 'file', size_bytes: 2048 },
				{ name: 'Directory', path: 'folder', type: 'directory' },
				{ name: 'Escape', path: '../secret', type: 'file' },
				{ name: 'Absolute', path: '/etc/shadow', type: 'file' }
			]
		})),
		queue: () => ({ ok: true, error: null, entries: ['movie.mkv', '<script>.mkv'] }),
		storage: () => ({ ok: true, error: null, mounted: true, writable: true, upload: true, delete: true,
			reason: null, total_bytes: 16 * 1024 ** 3, free_bytes: 10 * 1024 ** 3,
			reserve_bytes: 256 * 1024 ** 2, usable_bytes: 9.75 * 1024 ** 3, max_file_bytes: 9.75 * 1024 ** 3,
			chunk_size: 1048576, max_uploads: 2, active_uploads: 0 }),
		upload_begin: (location, size) => ({ ok: true, error: null, token: 'a'.repeat(64),
			offset: 0, size, path: location, chunk_size: 1048576, expires_in: 900, max_size: 9 * 1024 ** 3 }),
		remove: location => ({ ok: true, error: null, path: location })
	};
	const context = vm.createContext({
		E: (tag, attrs, children) => new Element(tag, attrs, children),
		_: value => value, L: { hasViewPermission: () => writable, env: { sessionid: '0'.repeat(32), cgi_base: '/cgi-bin' } }, document,
		window: { confirm: message => { confirmations.push(message); return confirmed; } },
		request: { post: (url, data, options) => {
			// Match installed LuCI request(): the cache-buster affects POST too.
			const record = { url: options.cache ? url : url + '?1700000000000', originalUrl: url, data, options };
			requests.push(record);
			if (!transport) throw new Error('Unexpected HTTP request in test');
			return transport(record);
		} },
		view: { extend: value => value }, poll: { add: (handler, interval) => polling.push({ handler, interval }) },
		rpc: { declare: spec => {
			declarations[spec.method] = JSON.parse(JSON.stringify(spec));
			return (...args) => {
				calls.push({ method: spec.method, args: JSON.parse(JSON.stringify(args)) });
				try {
					return Promise.resolve(responses[spec.method] ? responses[spec.method](...args) : { ok: true, error: null });
				} catch (error) { return Promise.reject(error); }
			};
		} }
	});
	const view = new vm.Script('(function() {\n' + source + '\n})()').runInContext(context);
	return { view, calls, declarations, polling, document, responses, requests, confirmations, env: context.L.env,
		setConfirmed: value => { confirmed = value; } };
}

async function settle() {
	for (let i = 0; i < 8; i++) await Promise.resolve();
}

async function run() {
	const contract = readJSON('api-contract.json');
	assert.equal(contract.object, 'airbridge.media');
	assert.deepEqual(contract.methods, methods);
	const acl = readJSON('root/usr/share/rpcd/acl.d/luci-app-airbridge-media.json')['luci-app-airbridge-media'];
	assert.deepEqual(Object.keys(acl).sort(), ['description', 'read', 'write']);
	for (const section of ['read', 'write']) {
		assert.deepEqual(Object.keys(acl[section]), ['ubus']);
		assert.deepEqual(Object.keys(acl[section].ubus), ['airbridge.media']);
		assert(!acl[section].ubus['airbridge.media'].includes('*'));
	}
	assert.deepEqual(acl.read.ubus['airbridge.media'], ['status', 'browse', 'storage']);
	assert.deepEqual(acl.read.ubus['airbridge.media'], contract.read_methods);
	assert.deepEqual(acl.write.ubus['airbridge.media'], contract.write_methods);
	assert.deepEqual([...acl.write.ubus['airbridge.media']].sort(), Object.keys(methods).filter(name => !['status', 'browse', 'storage'].includes(name)).sort());
	const menu = readJSON('root/usr/share/luci/menu.d/luci-app-airbridge-media.json')['admin/services/airbridge-media'];
	assert.equal(menu.action.type, 'view');
	assert.equal(menu.action.path, 'airbridge/media');
	assert.deepEqual(menu.depends.acl, ['luci-app-airbridge-media']);
	assert(!/\b(?:innerHTML|outerHTML|insertAdjacentHTML|eval|fetch|XMLHttpRequest)\b/.test(source));
	assert(!/['"]require (?:fs|uci)['"]/.test(source));

	const h = harness();
	for (const [method, params] of Object.entries(methods)) {
		assert.equal(h.declarations[method].object, 'airbridge.media');
		assert.equal(h.declarations[method].reject, true);
		assert.deepEqual(h.declarations[method].params, params);
	}
	const tree = h.view.render(await h.view.load());
	await settle();
	assert.equal(h.polling[0].interval, 2);
	assert.match(tree.textContent, /4K HDMI acceptance is a manual check/);
	assert.equal(h.view.title.textContent, '<img src=x onerror=alert(1)>');
	assert.equal(descendants(tree, node => ['img', 'script'].includes(node.tag)).length, 0);
	assert.equal(h.view.library.children.length, 2, 'reject unsafe entry paths');
	assert.match(h.view.library.textContent, /<img src=x onerror=alert\(1\)>.mkv/);
	assert.equal(h.view.previous.disabled, true);
	assert.equal(h.view.next.disabled, false);
	assert.equal(h.view.parent.disabled, true);
	assert.equal(h.view.audio.options.length, 2);
	assert.equal(h.view.audio.options[0].textContent, 'Off');
	assert.match(h.view.audio.options[1].textContent, /<script>evil/);
	assert.match(h.view.queue.textContent, /<script>.mkv/);
	assert.match(h.view.telemetry.textContent, /0:01:05 \/ 1:00:00/);
	const button = label => descendants(tree, node => node.tag === 'button' && node.textContent === label)[0];
	await button('Play').trigger('click');
	assert(h.calls.some(call => call.method === 'play' && call.args[0] === 'movie.mkv'));
	await h.view.pause.trigger('click');
	assert(h.calls.some(call => call.method === 'pause' && call.args[0] === true));
	h.view.updateStatus(state({ state: 'paused' }));
	assert.equal(h.view.pause.textContent, 'Resume');
	await h.view.pause.trigger('click');
	assert(h.calls.some(call => call.method === 'pause' && call.args[0] === false));
	await button('Back 10 seconds').trigger('click');
	assert(h.calls.some(call => call.method === 'seek' && call.args[0] === -10 && call.args[1] === true));
	h.view.seekInput.value = '72';
	await button('Seek').trigger('click');
	assert(h.calls.some(call => call.method === 'seek' && call.args[0] === 72 && call.args[1] === false));
	h.view.seekInput.value = '-1';
	const beforeInvalid = h.calls.length;
	await button('Seek').trigger('click');
	assert.equal(h.calls.length, beforeInvalid);
	h.view.volume.value = '77';
	await h.view.volume.trigger('change');
	assert(h.calls.some(call => call.method === 'volume' && call.args[0] === 77 && call.args[1] === false));
	h.view.mute.checked = true;
	await h.view.mute.trigger('change');
	assert(h.calls.some(call => call.method === 'volume' && call.args[1] === true));
	h.view.audio.value = '1';
	await h.view.audio.trigger('change');
	assert(h.calls.some(call => call.method === 'tracks' && call.args[0] === 'audio' && call.args[1] === 1));
	h.view.subtitles.value = '0';
	await h.view.subtitles.trigger('change');
	assert(h.calls.some(call => call.method === 'tracks' && call.args[0] === 'sub' && call.args[1] === 0));
	h.view.profile.value = 'quality4k';
	await h.view.profile.trigger('change');
	assert(h.calls.some(call => call.method === 'profile' && call.args[0] === 'quality4k'));
	await button('Add to queue').trigger('click');
	assert(h.calls.some(call => call.method === 'queue' && call.args[0] === 'append' && call.args[1] === 'movie.mkv'));
	await button('Next item').trigger('click');
	await button('Clear queue').trigger('click');
	assert(h.calls.some(call => call.method === 'queue' && call.args[0] === 'next'));
	assert(h.calls.some(call => call.method === 'queue' && call.args[0] === 'clear'));
	const count = h.view.controls.length;
	await h.view.browse('', 50);
	assert.equal(h.view.controls.length, count, 'do not leak controls across pagination');
	assert.equal(h.view.previous.disabled, false);
	assert.equal(h.view.next.disabled, true);
	assert(h.calls.some(call => call.method === 'browse' && call.args.join(',') === ',50,50'));
	const beforePath = h.calls.length;
	for (const location of ['/etc', '../x', 'x/../y', 'x\\y', 'x//y', 'x\0y', 'file:movie.mkv'])
		await h.view.browse(location, 0);
	assert.equal(h.calls.length, beforePath);
	h.responses.play = () => ({ ok: false, error: '<b>denied</b>' });
	await h.view.command('play', ['movie.mkv']);
	assert.equal(h.view.failure.textContent, '<b>denied</b>');
	h.responses.status = () => { throw new Error('ubus disconnected'); };
	await h.polling[0].handler();
	assert.equal(h.view.status.state, 'unavailable');
	assert.equal(h.view.pause.disabled, true);
	assert.match(h.view.engineError.textContent, /ubus disconnected/);

	const unavailable = harness({ status: state({ available: false, state: 'unavailable', error: 'Engine missing' }) });
	unavailable.view.render(await unavailable.view.load());
	await settle();
	assert.equal(unavailable.view.status.capabilities.browse, true, 'preserve status capabilities on engine unavailability');
	assert.equal(unavailable.view.pause.disabled, true);
	assert.equal(unavailable.view.parent.disabled, true);
	assert.equal(unavailable.view.controls.find(control => control.node.textContent === 'Refresh library').node.disabled, false);
	const beforeUnavailable = unavailable.calls.length;
	await unavailable.view.command('play', ['movie.mkv']);
	assert.equal(unavailable.calls.length, beforeUnavailable);

	const ro = harness({ writable: false });
	ro.view.render(await ro.view.load());
	await settle();
	assert.equal(ro.view.pause.disabled, true);
	assert(!ro.calls.some(call => call.method === 'queue'), 'no queue RPC for read-only sessions');
	const beforeReadOnly = ro.calls.length;
	await ro.view.command('play', ['movie.mkv']);
	assert.equal(ro.calls.length, beforeReadOnly);
	const unsupported = harness({ status: state({ capabilities: { browse: true } }) });
	unsupported.view.render(await unsupported.view.load());
	await settle();
	assert.equal(unsupported.view.profile.disabled, true);
	assert.equal(unsupported.view.audio.disabled, true);
	assert.equal(unsupported.view.volume.disabled, true);
	assert.equal(unsupported.view.mute.disabled, true);
	assert.equal(unsupported.view.pause.disabled, true);
	const independent = harness({ status: state({
		capabilities: { browse: true, volume: true, mute: false }
	}) });
	independent.view.render(await independent.view.load());
	await settle();
	assert.equal(independent.view.volume.disabled, false);
	assert.equal(independent.view.mute.disabled, true);
	await independent.view.command('volume', [55, false]);
	assert(independent.calls.some(call => call.method === 'volume' && call.args[0] === 55));
	const beforeMute = independent.calls.length;
	await independent.view.command('volume', [55, true]);
	assert.equal(independent.calls.length, beforeMute, 'mute is independently capability-gated');

	const race = harness();
	race.view.render(await race.view.load());
	await settle();
	let finishOld;
	race.responses.browse = location => location === 'old' ? new Promise(resolve => {
		finishOld = resolve;
	}) : { ok: true, error: null, path: location, offset: 0, more: false, entries: [] };
	const oldRequest = race.view.browse('old', 0);
	await race.view.browse('new', 0);
	finishOld({ ok: true, error: null, path: 'old', offset: 0, more: false, entries: [] });
	await oldRequest;
	assert.equal(race.view.path, 'new', 'stale browse must not overwrite new navigation');
	assert.match(race.view.library.textContent, /No media entries/);
	race.responses.browse = location => ({ ok: true, error: null, path: location,
		offset: 0, more: true, scan_limited: true, entries: [] });
	await race.view.browse('huge', 0);
	assert.equal(race.view.next.disabled, true);
	assert.match(race.view.browseNotice.textContent, /Directory scan limit reached/);
	race.responses.status = () => ({ ok: true, state: 'invented', available: true, capabilities: {} });
	await race.view.refreshStatus();
	assert.equal(race.view.status.state, 'unavailable');
	assert.match(race.view.engineError.textContent, /Invalid status/);
	const missing = harness();
	missing.responses.status = () => { throw new Error('object not found'); };
	missing.view.render(await missing.view.load());
	await settle();
	assert.equal(missing.view.pause.disabled, true);
	assert.match(missing.view.engineError.textContent, /object not found/);
	console.log('PASS: RPC schema, narrow ACL/menu, safe DOM/XSS labels, pagination, controls, queue, read-only, unavailable and capability gating');
	console.log('Hardware/HDMI/4K playback not tested.');
}

module.exports = { harness, state, settle, Element, descendants };
if (require.main === module)
	run().catch(error => {
		console.error(error);
		process.exitCode = 1;
	});
