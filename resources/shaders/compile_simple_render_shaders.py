import os
import time
import subprocess
import pathlib

def fromDir(dir):
    return list(filter(lambda n: not n.endswith('.spv') and not n.endswith('.h'), map(lambda n: dir + "/" + n, os.listdir(dir))))

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = fromDir('geometry') + fromDir('lighting')\
        + ["culling.comp", "landscape_culling.comp", "quad3_vert.vert"]
    
    for shader in shader_list:
        output = f"{shader}.spv"
        if not os.path.exists(output) or os.path.getmtime(shader) > os.path.getmtime(output):
            subprocess.run([glslang_cmd, "-V", "-g", shader, "-o", output])
        else:
            print(f"Up to date: {shader}")
