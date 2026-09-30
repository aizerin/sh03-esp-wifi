#!/usr/bin/env python3
"""Build without the dump, then check a rapid profile edit changes only its byte."""
from pathlib import Path
import tempfile,shutil,subprocess,json,hashlib,sys
ROOT=Path(__file__).resolve().parents[1]

def main():
    clean=Path(tempfile.mkdtemp(prefix='sh03-source-only-'))
    # Deliberately exclude every assembler file, all analysis, all existing build
    # products, and the original dump. No downloads occur during this build.
    for name in ['include','config','vendor']:shutil.copytree(ROOT/name,clean/name)
    (clean/'src').mkdir();(clean/'tools').mkdir()
    for path in (ROOT/'src').glob('*.c'):shutil.copy2(path,clean/'src'/path.name)
    for name in ['build_standalone.py','build_config.py']:shutil.copy2(ROOT/'tools'/name,clean/'tools'/name)
    shutil.copy2(ROOT/'standalone.ld',clean/'standalone.ld')
    subprocess.run([sys.executable,'tools/build_standalone.py'],cwd=clean,check=True)
    current=(ROOT/'build/standalone/firmware.bin').read_bytes()
    assert (clean/'build/standalone/firmware.bin').read_bytes()==current
    profiles=json.loads((clean/'config/materials.json').read_text());profiles[0]['temperature_c']=51
    (clean/'config/materials.json').write_text(json.dumps(profiles)+'\n')
    subprocess.run([sys.executable,'tools/build_standalone.py'],cwd=clean,check=True)
    modified=(clean/'build/standalone/firmware.bin').read_bytes()
    assert len(modified)==len(current)
    assert [i for i,(a,b) in enumerate(zip(current,modified)) if a!=b]==[0x10bb1]
    result={'status':'PASS','candidate_sha256':hashlib.sha256(current).hexdigest(),'without_dump':True,
            'without_firmware_assembly':True,'without_analysis_or_build_products':True,'offline_build':True,
            'profile_edit_changed_addresses':['08010bb1']}
    (ROOT/'analysis/standalone').mkdir(parents=True,exist_ok=True)
    (ROOT/'analysis/standalone/independent-build-test.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
