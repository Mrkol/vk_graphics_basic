import os
import subprocess
import pathlib

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = ["deferred.vert", "deferred.frag", "lighting.vert", "lighting.geom", "lighting.frag", "culling.comp", "wireframe.geom", "wireframe.frag", "deferred_landscape.vert", "deferred_landscape.tesc", "deferred_landscape.tese", "deferred_landscape.frag"]

    for shader in shader_list:
        subprocess.run([glslang_cmd, "-V", "-g", shader, "-o", "{}.spv".format(shader)])

