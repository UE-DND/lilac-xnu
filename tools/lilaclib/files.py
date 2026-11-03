import ctypes
import os
import shutil


def copy_file(source, target):
    if os.uname().sysname == 'Darwin':
        library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
        if library.clonefile(os.fsencode(source), os.fsencode(target), 0) == 0:
            shutil.copystat(source, target)
            return
    shutil.copy2(source, target)
