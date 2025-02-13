# base type is macOS and linux, only test base and core library
# -p platform: "base", "iOS", "Android"
# -t build_type: "Debug", "Release"
# -o output: "lib", "exe" build library or executable
# -d disable component: "audio" or more
# -a clear: clear cmake cache
#  gen: generate project file build: generate and compile run: if executable file, build and run
# disable_test

import os
import shutil
import argparse
import json

now_path = os.getcwd()
root_path = os.path.abspath(os.path.dirname(now_path))
build_path = os.path.join(root_path, "build")
config_file = os.path.join(root_path, "config.json")

config_data = {
    "platform": "PC",
    "build_type": "Debug",
    "output": "exe",
    "disable_http": False,
    "disable_test": False
}


def delete_cmake_cache(file_path):
    print("-------clear start -------\n")
    for root_paths, dirs, files in os.walk(file_path):
        for file in files:
            if any(ext in file for ext in ["CMakeCache", "cmake_install", ".a", "Makefile", ".dylib"]):
                print("remove files:" + os.path.join(root_paths, file))
                os.remove(os.path.join(root_paths, file))
        for path in dirs:
            if any(ext in path for ext in ["CMakeFiles", "_deps"]):
                print("remove dirs:" + os.path.join(root_paths, path))
                shutil.rmtree(os.path.join(root_paths, path))
    print("-------clear end -------\n")


def gen(platform, need_build_demo):
    print("-------gen start -------\n")
    if platform == "iOS":
        command = "cmake .. -B {} -G Xcode " \
                  "-DCMAKE_TOOLCHAIN_FILE=../ios.toolchain.cmake " \
                  "-DPLATFORM=OS64 " \
                  "-DENABLE_BITCODE=FALSE " \
                  "-DDEPLOYMENT_TARGET=16.3 ".format(build_path)
        print(command)
        os.system(command)
        if need_build_demo:
            command = "cd ../demo/iOS/demo && pod install --repo-update"
            os.system(command)
            print(command)
            command = "cd ../demo/iOS/demo && open demo.xcworkspace"
        else:
            command = "open ../build/slark.xcodeproj"

    elif platform == "Android":
        command = "cmake -B build -G Ninja " \
                  "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake " \
                  "-DANDROID_ABI=arm64-v8a " \
                  "-DANDROID_PLATFORM=29 "
        print(command)
        os.system(command)
    else:
        command = "cmake -DCMAKE_MAKE_PROGRAM=/usr/bin/make -S {} -B {} -G 'Unix Makefiles'".format(root_path,
                                                                                                    build_path)
    print(command)
    os.system(command)
    print("-------gen end -------\n")


def build(platform):
    print("-------build start -------\n")
    command = ""
    if platform == "iOS":
        command = "xcodebuild -project slark.xcodeproj"
    elif platform == "Android":
        command = "cmake --build {}".format(build_path)
    print(command)
    os.system(command)
    print("-------build end -------\n")


def parse_args():
    parser = argparse.ArgumentParser(description="slark build script")
    parser.add_argument('-p', '--platform', action='store', help="build platform", default="PC",
                        choices=["PC", "iOS", "Android"])
    parser.add_argument('-t', '--type', action='store', help="build type", default="Debug",
                        choices=["Debug", "Release"])
    parser.add_argument('-o', '--output', action='store', help="output type", default="exe", choices=["lib", "exe"])
    parser.add_argument('-disable_http', action='store_true', help="disable http")
    parser.add_argument('-a', '--action', action='store', help="action", choices=["clear", "gen", "build"],
                        default="gen")
    parser.add_argument('-d', '--demo', action='store_true', help="build demo")
    parser.add_argument('-disable_test', action='store_true', help="disable test")
    args = parser.parse_args()

    config_data["platform"] = args.platform
    config_data["build_type"] = args.type
    config_data["output"] = args.output
    config_data["disable_http"] = args.disable_http
    config_data["disable_test"] = args.disable_test
    need_gen_demo = args.demo

    with open(config_file, mode='w', encoding='utf-8') as f:
        json.dump(config_data, f, indent=4)
        json_data = json.dumps(config_data, indent=4, ensure_ascii=True)
        print("------ build config ----- \n" + json_data)

    action_list = ["clear", "gen", "build"]
    func_list = [lambda: delete_cmake_cache(root_path),
                 lambda: gen(args.platform, need_gen_demo),
                 lambda: build(args.platform),
                 ]
    func_index = action_list.index(args.action)
    index = 0
    while index <= func_index:
        func_list[index]()
        index += 1


def main():
    parse_args()


if __name__ == '__main__':
    main()
