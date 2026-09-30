#!/usr/bin/env python3
"""Run the real C transaction core with NOR semantics and power-cut injection."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix='sh03-update-test-') as directory:
        binary=str(Path(directory)/'test')
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'include'),
            str(ROOT/'bootloader/update.c'),str(ROOT/'tests/update_test.c'),'-o',binary],check=True)
        subprocess.run([binary,str(ROOT/'build/ota/factory.bin'),str(ROOT/'build/ota/application.sh03')],check=True)

if __name__=='__main__':main()
