import os
import shutil
import sys

def delete_cmake_cache(file_path):
    for root_path, dirs, files in os.walk(file_path):
        for file in files:
            if file.find("CMakeCache") >= 0 or \
                file.find("cmake_install") >= 0 or \
                file.find(".a") >= 0 or \
                file.find("Makefile") >= 0 or \
                file.find(".dylib") >= 0:
                print("remove files:" + os.path.join(root_path, file))
                os.remove(os.path.join(root_path, file))

        for path in dirs:
            if path.find("CMakeFiles") >= 0:
                print("remove dirs:" + os.path.join(root_path, path))
                shutil.rmtree(os.path.join(root_path, path))


def get_target_path(res):
    str = "Build files have been written to:"
    f = res.find(str)
    if f >= 0:
        return res[f + len(str):]
    else:
        return

now_path = os.getcwd()
root_path = os.path.abspath(os.path.dirname(now_path))
build_path = root_path + "/build"
print(root_path)
delete_cmake_cache(root_path)

res = 0
if len(sys.argv) >= 2 and (sys.argv[1] == "-b" or sys.argv[1] == "-r"):
    command = "cmake -S" + root_path + " -B " + build_path + " -DCMAKE_BUILD_TYPE="
    if len(sys.argv) >= 3:
        command += sys.argv[2]
    else:
        command += "Debug"
    print(command)
    res = os.system(command)
    command = "cmake --build " + build_path
    print(command)
    res = os.system(command)

exec_path = "./bin/slark"
if res == 0 and len(sys.argv) >= 2 and sys.argv[1] == "-r":
    res = os.popen("chmod 777 " + exec_path)
    res = os.system(exec_path)

