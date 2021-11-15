import os
import subprocess
import pathlib

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = ["deferred.vert", "deferred.frag", "lighting.vert", "lighting.geom", "lighting.frag", "culling.comp", "wireframe.geom", "wireframe.frag"]

    for shader in shader_list:
        subprocess.run([glslang_cmd, "-V", "-g", shader, "-o", "{}.spv".format(shader)])

