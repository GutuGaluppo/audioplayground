// The browser host as the AudioWorklet runs it, without a browser: the compiled module, loaded by
// the same engine.js. Build it first (tools/web/build.sh).
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { WasmEngine } from "../../../ui/public/engine/engine.js";

const wasmPath = fileURLToPath(
    new URL("../../../ui/public/engine/ap_web.wasm", import.meta.url),
);
const module = await WebAssembly.compile(await readFile(wasmPath));

const create = (options) =>
    WasmEngine.create(module, {
        sampleRate: 48000,
        blockSize: 128,
        ...options,
    });
const send = (engine, type, payload = {}) => engine.intent({ type, payload });

function render(engine, seconds, chunk = 128) {
    const frames = Math.round(seconds * engine.sampleRate);
    const out = [new Float32Array(frames), new Float32Array(frames)];
    for (let done = 0; done < frames; done += chunk) {
        const n = Math.min(chunk, frames - done);
        const left = new Float32Array(n);
        const right = new Float32Array(n);
        engine.process(left, right);
        out[0].set(left, done);
        out[1].set(right, done);
    }
    return out;
}

const energy = (channel) => channel.reduce((sum, x) => sum + x * x, 0);
const typesOf = (events) => new Set(events.map((e) => e.type));
const last = (events, type) =>
    events.filter((e) => e.type === type).at(-1)?.payload;

test("app.ready describes the whole starter song", async () => {
    const engine = await create();
    assert.equal(send(engine, "app.ready"), true);
    const events = engine.takeEvents();
    for (const type of [
        "engine.status",
        "transport.state",
        "transport.position",
        "param.value",
        "history.state",
        "project.state",
        "instrument.state",
        "drums.kit",
        "drums.pad",
        "timeline.state",
        "project.assets",
        "export.state",
        "accompaniment.state",
    ])
        assert.ok(typesOf(events).has(type), `missing ${type}`);

    const timeline = last(events, "timeline.state");
    assert.equal(timeline.tracks.length, 2); // a drum track with the beat and an empty synth track
    assert.ok(timeline.tracks[0].clips[0].notes.length > 0);
    assert.equal(last(events, "engine.status").deviceName, "Web Audio");
    assert.equal(
        events.filter((e) => e.type === "param.value").length >= 10,
        true,
    );
    assert.deepEqual(engine.takeEvents(), []); // nothing left over
});

test("the starter beat plays, with meters and a moving playhead", async () => {
    const engine = await create();
    send(engine, "app.ready");
    engine.takeEvents();
    assert.equal(energy(render(engine, 0.5)[0]), 0); // not playing yet: silence

    send(engine, "transport.play");
    const audio = render(engine, 2);
    assert.ok(energy(audio[0]) > 0.01, "the beat is audible");
    assert.ok(energy(audio[1]) > 0.01);
    assert.ok(audio[0].every(Number.isFinite));

    const events = engine.takeEvents();
    assert.equal(last(events, "transport.state").playing, true);
    assert.ok(
        Math.max(
            ...events
                .filter((e) => e.type === "engine.meters")
                .map((e) => e.payload.peak),
        ) > 0.01,
    );
    const positions = events
        .filter((e) => e.type === "transport.position")
        .map((e) => e.payload.ticks);
    assert.ok(positions.length > 3);
    assert.ok(positions.at(-1) > positions[0]);

    send(engine, "transport.stop");
    render(engine, 0.1); // the audio thread takes the request at its next block, then the page is told
    assert.equal(last(engine.takeEvents(), "transport.state").playing, false);
});

test("a played note sounds, with the synth chosen from the page", async () => {
    const engine = await create();
    send(engine, "app.ready");
    send(engine, "instrument.select", { instrument: 0 });
    send(engine, "note.on", { note: 60, velocity: 0.8 });
    const audio = render(engine, 0.5);
    assert.ok(energy(audio[0]) > 0.001);
    send(engine, "note.off", { note: 60 });
    send(engine, "note.allOff");
    assert.equal(last(engine.takeEvents(), "instrument.state").instrument, 0);
});

test("edits come back as events, and undo and redo work", async () => {
    const engine = await create();
    send(engine, "app.ready");
    engine.takeEvents();

    send(engine, "param.set", { id: "synth.cutoff", value: 1500, gesture: 0 });
    let events = engine.takeEvents();
    assert.equal(
        events.find(
            (e) => e.type === "param.value" && e.payload.id === "synth.cutoff",
        ).payload.value,
        1500,
    );
    assert.equal(last(events, "history.state").canUndo, true);

    send(engine, "drums.setStep", {
        clip: 0,
        pad: 5,
        step: 3,
        on: true,
        gesture: 0,
    });
    events = engine.takeEvents();
    const clip = last(events, "timeline.state").tracks[0].clips[0];
    assert.ok(
        clip.notes.some((n) => n.pitch === 36 + 5 && n.start === 3 * 240),
    );

    send(engine, "edit.undo");
    events = engine.takeEvents();
    assert.ok(
        !last(events, "timeline.state").tracks[0].clips[0].notes.some(
            (n) => n.pitch === 41 && n.start === 720,
        ),
    );
    send(engine, "edit.undo");
    events = engine.takeEvents();
    assert.equal(
        events.find(
            (e) => e.type === "param.value" && e.payload.id === "synth.cutoff",
        ).payload.value,
        4000,
    );
    assert.equal(last(events, "history.state").canRedo, true);
    send(engine, "edit.redo");
    assert.equal(
        engine
            .takeEvents()
            .find(
                (e) =>
                    e.type === "param.value" && e.payload.id === "synth.cutoff",
            ).payload.value,
        1500,
    );
});

test("a refused change is echoed so the page never keeps a value the core did not accept", async () => {
    const engine = await create();
    send(engine, "app.ready");
    engine.takeEvents();
    send(engine, "param.set", { id: "synth.cutoff", value: 4000, gesture: 0 }); // already there
    assert.equal(
        engine.takeEvents().find((e) => e.type === "param.value").payload.value,
        4000,
    );
    send(engine, "track.rename", { track: 999, name: "x" }); // no such track
    assert.ok(typesOf(engine.takeEvents()).has("timeline.state"));
});

test("malformed and oversized messages are dropped, and what the browser lacks says so", async () => {
    const engine = await create();
    send(engine, "app.ready");
    engine.takeEvents();

    assert.equal(send(engine, "track.add", { kind: 99 }), false);
    assert.equal(send(engine, "track.add", { kind: 1, extra: true }), false);
    assert.equal(send(engine, "no.such.intent"), false);
    assert.equal(
        engine.intent({ type: "transport.setTempo", payload: { bpm: 5000 } }),
        false,
    );
    assert.equal(
        send(engine, "drums.setPattern", {
            clip: 0,
            pads: new Array(1000).fill(1),
        }),
        false,
    );
    assert.equal(
        engine.intent({
            type: "project.rename",
            payload: { name: "x".repeat(2 * 1024 * 1024) },
        }),
        false,
    );
    assert.deepEqual(engine.takeEvents(), []);

    assert.equal(send(engine, "project.save"), true);
    const notice = last(engine.takeEvents(), "app.notice");
    assert.match(notice.message, /not available in the browser/);

    // Text that is not JSON never reaches the codec.
    const text = new TextEncoder().encode(
        '{"type": "track.add", "payload": {"kind": 1',
    );
    new Uint8Array(engine.memory, engine.scratch, text.length).set(text);
    assert.equal(engine.exports.ap_intent(engine.scratch, text.length), 0);
});

test("unicode in names survives the round trip", async () => {
    const engine = await create();
    send(engine, "app.ready");
    engine.takeEvents();
    send(engine, "project.rename", { name: "Canção — 曲 🎵" });
    assert.equal(
        last(engine.takeEvents(), "project.state").name,
        "Canção — 曲 🎵",
    );
});

test("rendering is deterministic and does not depend on how the audio is pulled", async () => {
    const play = async (chunk) => {
        const engine = await create();
        send(engine, "app.ready");
        send(engine, "transport.play");
        return render(engine, 2, chunk);
    };
    const a = await play(128);
    const b = await play(128);
    const c = await play(500); // not a multiple of the engine's block
    assert.deepEqual(a[0], b[0]);
    assert.deepEqual(a[0], c[0]);
    assert.deepEqual(a[1], c[1]);
});

test("a long session stays bounded", async () => {
    const engine = await create();
    send(engine, "app.ready");
    send(engine, "transport.play");
    const before = engine.memory.byteLength;
    for (let second = 0; second < 30; second++) {
        render(engine, 1);
        engine.takeEvents();
    }
    assert.ok(
        engine.memory.byteLength <= before * 2,
        "memory did not run away",
    );
});

test("the engine refuses unreasonable settings", async () => {
    await assert.rejects(
        WasmEngine.create(module, { sampleRate: 100, blockSize: 128 }),
    );
    await assert.rejects(
        WasmEngine.create(module, { sampleRate: 48000, blockSize: 0 }),
    );
});
