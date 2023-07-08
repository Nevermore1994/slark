
import os
import shutil
import argparse
import json

# base type is macOS and linux, only test base and core library
# -p platform: "base", "iOS", "Android"
# -t build_type: "Debug", "Release"
# -o output: "lib", "exe" build library or executable
# -n "component": "base", "core"
# -d disable component: "audio" or more
# -a clear: clear cmake cache
#  gen: generate project file build: generate and compile run: if executable file, build and run
# disable_test

# global path
now_path = os.getcwd()
root_path = os.path.abspath(os.path.dirname(now_path))
build_path = root_path + "/build"

parser = argparse.ArgumentParser(description="slark build script")

# base config
config_data = {
    # base type is macOS and linux, only test base and core library 
    # -p platform: "base", "iOS", "Android"
    "platform": "base",
    # -t build_type: "Debug", "Release"
    "build_type": "Debug",
    # -o library: "lib", "exe" build library or executable
    "output": "exe",
    # -n "component": "base", "core"
    "component": {
        "main": "base",
        # -d  disable component: "audio" or more 
        "disable": []
    },
    "disable_test": 0
}


# remove cmake cache
def delete_cmake_cache(file_path):
    print("-------clear start -------\n")
    for root_paths, dirs, files in os.walk(file_path):
        for file in files:
            if file.find("CMakeCache") >= 0 or \
                    file.find("cmake_install") >= 0 or \
                    file.find(".a") >= 0 or \
                    file.find("Makefile") >= 0 or \
                    file.find(".dylib") >= 0:
                print("remove files:" + os.path.join(root_paths, file))
                os.remove(os.path.join(root_paths, file))

        for path in dirs:
            if path.find("CMakeFiles") >= 0 or path.find("_deps") >= 0:
                print("remove dirs:" + os.path.join(root_paths, path))
                shutil.rmtree(os.path.join(root_paths, path))
    print("-------clear end -------\n")


def gen(platform):
    print("-------gen start -------\n")
    command = "cmake -DCMAKE_MAKE_PROGRAM=/usr/bin/make -S" + root_path + " -B " + build_path + " -G "
    if platform == "iOS":
        command = "cmake .. -G Xcode"
        print(command)
        os.system(command)
        command = "open slark.xcodeproj"
    else:
        command += '"Unix Makefiles"'
    print(command)
    os.system(command)
    print("-------gen end -------\n")


def build(platform):
    print("-------build start -------\n")
    if platform == "iOS":
        command = "xcode build" + "slark.xcodeproj"
    else:
        command = "cmake --build " + build_path
    print(command)
    os.system(command)
    print("-------build end -------\n")


def run():
    print("-------run -------")
    exec_path = "./bin/slark"
    if os.path.exists("./bin/slark"):
        os.popen("chmod 777 " + exec_path)
        os.system(exec_path)
    else:
        print("run:could not be launched.")


# set_setting
def set_setting():
    parser.add_argument('-v', '--version',
                        action='version',
                        version='%(prog)s version : v 0.01',
                        help='show the version')

    parser.add_argument('-p', '--platform',
                        action='store',
                        help="""build platform, "base" type is macOS or linux, only test base and core library, 
                        default = "base" """,
                        default="base",
                        choices=["base", "iOS", "Android"],
                        metavar="base")

    parser.add_argument('-t', '--type',
                        action='store',
                        help=""" build_type, default = Debug """,
                        default="Debug",
                        choices=["Debug", "Release"],
                        metavar="Debug")

    parser.add_argument('-o', '--output',
                        action='store',
                        help="""  output library or executable, default = exe """,
                        default="exe",
                        choices=["lib", "exe"],
                        metavar="exe")

    parser.add_argument('-n', '--component',
                        action='store',
                        help=""" component: base, core default = base """,
                        default="base",
                        choices=["base", "core"],
                        metavar="base")

    parser.add_argument('-d', '--disable',
                        action='store',
                        nargs='+',
                        help=""" disable component """,
                        choices=["audio", "http"],
                        metavar="audio")

    parser.add_argument('-a', '--action',
                        help=""" action 
                        clear: clear cmake cache
                        gen: generate project file
                        build: generate and compile
                        run: if executable file, build and run """,
                        choices=["clear", "gen", "build", "run"],
                        metavar="gen",
                        default="gen")
    parser.add_argument('-disable_test',
                        action='store_true',
                        help="disable test",
                        default=False)


def parse_args():
    args = parser.parse_args()
    config_data["platform"] = args.platform
    config_data["build_type"] = args.type
    config_data["output"] = args.output
    config_data["component"]["main"] = args.component

    if args.disable is not None:
        for remove_component in args.disable:
            config_data["component"]["disable"].remove(remove_component)

    if args.disable_test:
        config_data["disable_test"] = 1

    with open('./config.json', mode='w', encoding='utf-8') as f:
        json_data = json.dumps(config_data, indent=4, ensure_ascii=True)
        json.dump(config_data, f, indent=4)
        print("------ build config ----- \n" + json_data)

    action_list = ["clear", "gen", "build", "run"]
    func_list = [lambda: delete_cmake_cache(root_path),
                 lambda: gen(args.platform),
                 lambda: build(args.platform),
                 lambda: run()]
    func_index = action_list.index(args.action)
    index = 0
    while index <= func_index:
        func_list[index]()
        index += 1


def main():
    print(root_path)
    set_setting()
    parse_args()


if __name__ == '__main__':
    main()
