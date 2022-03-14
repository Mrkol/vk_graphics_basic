import os
import time
import subprocess
import pathlib

def fromDir(dir):
    return list(filter(lambda n: not n.endswith('.spv'), map(lambda n: dir + "/" + n, os.listdir(dir))))

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = fromDir('geometry') + fromDir('lighting') + ["culling.comp", "quad3_vert.vert"]
    
    for shader in shader_list:
        output = f"{shader}.spv"
        inTime = os.path.getmtime(shader)
        outTime = os.path.getmtime(output)
        if inTime > outTime:
            subprocess.run([glslang_cmd, "-V", "-g", shader, "-o", output])
        else:
            print(f"Up to date: {shader}")
