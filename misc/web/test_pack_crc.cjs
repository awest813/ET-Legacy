// Run the launcher's real streaming CRC implementation without downloading packs.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {performance} = require('node:perf_hooks');
const source = fs.readFileSync(`${__dirname}/etl_shell.html`, 'utf8');
const implementation = source.match(/var crcTable = [\s\S]*?(?=\n\tfunction verifyPack\()/)[0];
// Match the launcher's closure: global VM lookups distort the timing sample.
const runtime = vm.runInNewContext(`(function() { ${implementation}; return {updatePackCRC, crcTable}; })()`, {Uint32Array});
const update = runtime.updatePackCRC;
const hex = crc => ((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, '0');
const pattern = size => Uint8Array.from({length:size}, (_, i) => (i * 73 + (i >> 3) + 19) & 255);
// Expected values generated independently with Python zlib.crc32.
const fixtures = [[0,'00000000'],[1,'56bcae53'],[3,'51938104'],[4,'928c776a'],
    [5,'c74a4eb3'],[7,'c66ea606'],[8,'c81e5556'],[9,'321f18c6'],
    [255,'4d33ef1c'],[256,'1f98daf1'],[257,'a777d4cb'],
    [65539,'c98ff038'],[1048576,'1903e23b']];
assert.equal(hex(update(0xffffffff,Buffer.from('123456789'),9)), 'cbf43926');
for (const [size, expected] of fixtures) {
    const bytes = pattern(size);
    assert.equal(hex(update(0xffffffff, bytes, size)), expected, `whole file ${size}`);
    for (const chunk of [1,3,4,5,7,255,4093,8388608]) {
        let crc = 0xffffffff;
        // A nonzero byteOffset must not affect word ordering or read alignment.
        const storage = new Uint8Array(chunk + 7).fill(0xee);
        const view = storage.subarray(3, 3 + chunk);
        for (let offset = 0; offset < size; offset += chunk) {
            const count = Math.min(chunk, size - offset);
            view.set(bytes.subarray(offset, offset + count));
            crc = update(crc, view, count);
        }
        assert.equal(hex(crc), expected, `${size} bytes, ${chunk}-byte chunks`);
    }
}
// The CRC remains sensitive to corruption, including tails outside a full word.
const original = pattern(65539), expected = hex(update(0xffffffff, original, original.length));
for (const offset of [0,3,4,32768,65535,65536,65538]) {
    const changed = original.slice(); changed[offset] ^= 0x80;
    assert.notEqual(hex(update(0xffffffff, changed, changed.length)), expected);
}
console.log('Pack CRC: independent zlib fixtures, streaming boundaries, unaligned views, partial buffers and corruption checks passed.');
if (process.argv.includes('--benchmark')) {
    const bytes = pattern(8 * 1048576), table = runtime.crcTable;
    function previous(crc, bytes, count) {
        for (let n = 0; n < count; n++) crc = (crc >>> 8) ^ table[(crc ^ bytes[n]) & 255];
        return crc;
    }
    for (const fn of [previous, update]) for (let i = 0; i < 5; i++) fn(0xffffffff,bytes,bytes.length);
    for (let run = 0; run < 3; run++) for (const [name, fn] of [['bytewise',previous],['four-byte',update]]) {
        const begin = performance.now(); let crc = 0xffffffff;
        for (let i = 0; i < 32; i++) crc = fn(crc,bytes,bytes.length);
        console.log(`${name}: ${(performance.now() - begin).toFixed(1)} ms for 256 MiB, CRC ${hex(crc)}`);
    }
}
