"""Build the indexed subject/SPKI CA format used by Arduino ESP32 2.0.17.
No network or third-party Python modules are required. Input is reviewed PEM.
"""
from pathlib import Path
import argparse,hashlib,json,re,ssl,struct

def tlv(data,position):
    start=position
    if position+2>len(data):raise ValueError('Truncated DER')
    tag=data[position];position+=1;length=data[position];position+=1
    if length&128:
        count=length&127
        if not count or count>4 or position+count>len(data):raise ValueError('Invalid DER length')
        length=int.from_bytes(data[position:position+count],'big');position+=count
    end=position+length
    if end>len(data):raise ValueError('DER value out of bounds')
    return tag,start,position,end

def subject_and_key(certificate):
    outer=tlv(certificate,0)
    if outer[0]!=0x30 or outer[3]!=len(certificate):raise ValueError('Invalid certificate sequence')
    tbs=tlv(certificate,outer[2])
    if tbs[0]!=0x30:raise ValueError('Invalid TBSCertificate')
    fields=[];position=tbs[2]
    while position<tbs[3]:
        field=tlv(certificate,position);fields.append(field);position=field[3]
    offset=1 if fields[0][0]==0xa0 else 0
    subject,key=fields[offset+4],fields[offset+5]
    if subject[0]!=0x30 or key[0]!=0x30:raise ValueError('Invalid subject/key sequence')
    return certificate[subject[1]:subject[3]],certificate[key[1]:key[3]]

def records_from_pem(data):
    if b'\0' in data:raise ValueError('PEM must not contain embedded NULs')
    blocks=re.findall(rb'-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----',data)
    if not blocks:raise ValueError('No PEM certificates')
    records={}
    for block in blocks:
        der=ssl.PEM_cert_to_DER_cert(block.decode('ascii'))
        subject,key=subject_and_key(der)
        if subject in records and records[subject]!=key:raise ValueError('Different trusted keys have the same subject; review needed')
        records[subject]=key
    return sorted(records.items())

def encode(records):
    if not records or len(records)>65535:raise ValueError('Invalid bundle count')
    if any(len(key)>2048 for _,key in records):raise ValueError('Root key exceeds the verifier export limit')
    data=bytearray(struct.pack('>H',len(records)))
    for subject,key in records:
        data.extend(struct.pack('>HH',len(subject),len(key)));data.extend(subject);data.extend(key)
    return bytes(data)

def decode(data):
    if len(data)<2:raise ValueError('Truncated bundle')
    count=struct.unpack('>H',data[:2])[0];records=[];position=2
    for _ in range(count):
        if position+4>len(data):raise ValueError('Truncated record header')
        subject_size,key_size=struct.unpack('>HH',data[position:position+4]);position+=4
        end=position+subject_size+key_size
        if not subject_size or not key_size or end>len(data):raise ValueError('Truncated record')
        records.append((data[position:position+subject_size],data[position+subject_size:end]));position=end
    if position!=len(data) or records!=sorted(records):raise ValueError('Unsorted or extra bundle data')
    return records

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=root/'certs/mozilla-ca.pem')
    parser.add_argument('--source-version',default='2026.10.23',help='reviewed certifi source version')
    parser.add_argument('--output',type=Path,default=root/'certs/x509_crt_bundle.bin')
    args=parser.parse_args()
    source=args.source.read_bytes();records=records_from_pem(source);binary=encode(records)
    if decode(binary)!=records:raise ValueError('Bundle round-trip failed')
    args.output.write_bytes(binary)
    metadata={'source':'Mozilla CA roots distributed by certifi','source_version':args.source_version,
              'source_url':'https://github.com/certifi/python-certifi','format':'arduino-esp32-2.0.17-indexed-v1',
              'roots':len(records),'pem_sha256':hashlib.sha256(source).hexdigest(),'bundle_sha256':hashlib.sha256(binary).hexdigest()}
    args.output.with_name('bundle-info.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    header='#pragma once\nconstexpr const char *CA_BUNDLE_SOURCE_VERSION='+json.dumps(args.source_version)+';\n'
    header+='constexpr unsigned CA_BUNDLE_ROOT_COUNT='+str(len(records))+';\n'
    header+='constexpr const char *CA_BUNDLE_SHA256='+json.dumps(metadata['bundle_sha256'])+';\n'
    (root/'include/ca_bundle_info.h').write_text(header,encoding='ascii')
    print(f'Generated {len(records)} indexed CA roots, {len(binary)} bytes')
    print('PEM SHA256:',hashlib.sha256(source).hexdigest())
    print('Bundle SHA256:',hashlib.sha256(binary).hexdigest())
if __name__=='__main__':main()
