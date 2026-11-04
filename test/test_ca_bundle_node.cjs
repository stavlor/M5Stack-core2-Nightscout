// Independent OpenSSL-backed check of every embedded SPKI, via Node crypto.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.join(__dirname,'..');
const pem=fs.readFileSync(path.join(root,'certs/mozilla-ca.pem'),'ascii');
const certificates=pem.match(/-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----/g);
const expected=certificates.map(p=>crypto.createHash('sha256').update(new crypto.X509Certificate(p).publicKey.export({format:'der',type:'spki'})).digest('hex')).sort();
const bundle=fs.readFileSync(path.join(root,'certs/x509_crt_bundle.bin'));let position=2;const actual=[];
for(let i=0;i<bundle.readUInt16BE(0);i++){
 const subjectLength=bundle.readUInt16BE(position),keyLength=bundle.readUInt16BE(position+2);position+=4;
 assert.equal(bundle[position],0x30);position+=subjectLength;
 const spki=bundle.subarray(position,position+keyLength);position+=keyLength;
 crypto.createPublicKey({key:spki,format:'der',type:'spki'});
 actual.push(crypto.createHash('sha256').update(spki).digest('hex'));
}
assert.equal(position,bundle.length);assert.deepEqual(actual.sort(),expected);
console.log('All '+actual.length+' embedded public keys match the source certificates using Node/OpenSSL.');

const cross=new crypto.X509Certificate(fs.readFileSync(path.join(__dirname,'fixtures/gts-r1-cross-signed.pem')));
const source=certificates.map(p=>new crypto.X509Certificate(p));
const anchor=source.find(c=>c.subject===cross.subject);
assert(anchor);
assert(anchor.publicKey.export({format:'der',type:'spki'}).equals(cross.publicKey.export({format:'der',type:'spki'})));
assert.equal(source.some(c=>c.subject===cross.issuer),false);
assert.equal(cross.verify(cross.publicKey),false);
console.log('Cross-signed Google root has an exact trusted root key while its legacy parent is absent.');
